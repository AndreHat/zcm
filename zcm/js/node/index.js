/*******************************************************
 * NodeJS FFI bindings to ZCM
 * --------------------------
 * Could/Should be more efficent, but it should
 * suffice in the short-term
 ******************************************************/
var ffi = require('ffi');
var ref = require('ref');
var StructType = require('ref-struct');
var ArrayType = require('ref-array');

// Define some types
var voidRef = ref.refType('void')
var charRef = ref.refType('char')

var recvBuf = StructType({
    // Note: it is VERY important that this struct match the zcm_recv_buf_t struct in zcm.h
    data:  charRef,
    len:   ref.types.uint32,
    utime: ref.types.uint64,
    zcm:   voidRef,
});
var recvBufRef = ref.refType(recvBuf);

var subscription = StructType({
    // Note: it is VERY important that this struct match the zcm_sub_t struct in zcm.h
    channel:  ArrayType(ref.types.char),
    callback: voidRef,
    usr:      voidRef,
});
var subscriptionRef = ref.refType(subscription);

// Define our Foreign Function Interface to the zcm library
var libzcm = new ffi.Library('libzcm', {
    'zcm_create':      ['pointer', ['string']],
    'zcm_destroy':     ['void', ['pointer']],
    'zcm_publish':     ['int', ['pointer', 'string', 'pointer', 'int']],
    'zcm_subscribe':   ['pointer', ['pointer', 'string', 'pointer', 'pointer']],
    'zcm_unsubscribe': ['int', ['pointer', 'pointer']],
    'zcm_start':       ['void', ['pointer']],
    'zcm_stop':        ['void', ['pointer']],
});

/**
 * Callback that handles data received on the zcm transport which this program has subscribed to
 * @callback dispatchRawCallback
 * @param {string} channel - the zcm channel
 * @param {Buffer} data - raw data that can be decoded into a zcmtype
 */

/**
 * Callback that handles data received on the zcm transport which this program has subscribed to
 * @callback dispatchDecodedCallback
 * @param {string} channel - the zcm channel
 * @param {zcmtype} msg - a decoded zcmtype
 */

/**
 * Creates a dispatch function that can interface with the ffi library
 * @param {dispatchRawCallback} cb - the js callback function to be linked into the ffi library
 */
function makeDispatcher(cb)
{
    return function(rbuf, channel, usr) {
        // XXX This decoder makes a LOT of assumptions about the underlying machine
        //     These are not all correct for archs other than x86-64
        // Note: it is VERY important that this struct is decoded to match the zcm_recv_buf_t
        //       struct in zcm.h
        var data   = ref.readPointer(rbuf, 0);
        // Note: despite len being a 32 bit uint, the following seems to work (simply inspected
        //       by printing the interpreted utime, and I suspect it is due to the internal
        //       workings of FFI, might be worth verifying though
        var len    = ref.readUInt64LE(rbuf, 8);
        var utime  = ref.readUInt64LE(rbuf, 16);
        var zcmPtr = ref.readPointer(rbuf, 24);
        var dataBuf = ref.reinterpret(data, len);
        cb(channel, new Buffer(dataBuf));
    }
}

// TODO: consider adding support for server driven comms (would be easy to export bindings into
//       this library for the server to use instead of the client)
function libzcmTransport(transport) {
    var z = libzcm.zcm_create(transport);
    if (z.isNull()) {
        console.log("Err: Failed to create transport '"+transport+"'");
        return {};
    }

    libzcm.zcm_start(z);

    /**
     * Publishes a zcm message on the created transport
     * @param {string} channel - the zcm channel to publish on
     * @param {Buffer} data - the encoded message (use the encode function of a generated zcmtype)
     */
    function publish(channel, data) {
        libzcm.zcm_publish.async(z, channel, data, data.length, function(err, res) {});
    }

    /**
     * Subscribes to a zcm channel on the created transport
     * @param {string} channel - the zcm channel to subscribe to
     * @param {dispatchRawCallback} cb - callback to handle received messages
     * @returns {subscriptionRef} reference to the subscription, used to unsubscribe
     */
    function subscribe(channel, cb) {
        var funcPtr = ffi.Callback('void', [recvBufRef, 'string', 'pointer'], makeDispatcher(cb));
        return {"subscription" : libzcm.zcm_subscribe(z, channel, funcPtr, null),
                "nativeCallbackPtr" : funcPtr};
    }

    /**
     * Subscribes to all zcm channels on the created transport
     * @param {dispatchRawCallback} cb - callback to handle received messages
     * @returns {subscriptionRef} reference to the subscription, used to unsubscribe
     */
    function subscribe_all(cb) {
        return subscribe(".*", cb);
    }

    /**
     * Unsubscribes from the zcm channel referenced by the given subscription
     * @param {subscriptionRef} subscription - ref to the subscription to be unsubscribed from
     */
    function unsubscribe(subscription) {
        libzcm.zcm_unsubscribe(z, subscription.subscription);
    }

    return {
        publish:        publish,
        subscribe:      subscribe,
        subscribe_all:  subscribe_all,
        unsubscribe:    unsubscribe,
    };
}

// RRR: naming here is getting kind of confusing. The variable we refer to as zcmServer is
//      really the ffi interface class. This class is actually the server side interface to
//      zcm functions. I think we should either change the names to be more demonstrative or
//      potentially even merge the two classes. There really isn't much difference between
//      them and it seems like the extra layer might not be necessary.
function zcm (zcmtypes, zcmServer)
{
    var zcmServer = zcmServer;

    // RRR: I really like that we build the hashmap dynamically (instead of having
    //      zcmgen spit it out) because I think that will make it easier to load multiple
    //      zcmtypes files with something like spy. However, one drawback is that now the
    //      hashmap doesn't exist client side. Maybe it would be best to move this into the
    //      generated code and then just have a good way of merging multiple typemaps
    var zcmtypeHashMap = {};
    for (var type in zcmtypes)
        zcmtypeHashMap[zcmtypes[type].__hash] = zcmtypes[type];

    /**
     * Publishes a zcm message on the created transport
     * @param {string} channel - the zcm channel to publish on
     * @param {string} type - the zcmtype of messages on the channel (must be a generated
     *                        type from zcmtypes.js)
     * @param {Buffer} msg - the decoded message (must be a zcmtype)
     */
    function publish(channel, type, msg)
    {
        var _type = zcmtypes[type];
        zcmServer.publish(channel, _type.encode(msg));
    }

    /**
     * Subscribes to a zcm channel on the created transport
     * @param {string} channel - the zcm channel to subscribe to
     * @param {string} type - the zcmtype of messages on the channel (must be a generated
     *                        type from zcmtypes.js)
     * @param {dispatchDecodedCallback} cb - callback to handle received messages
     * @returns {subscriptionRef} reference to the subscription, used to unsubscribe
     */
    function subscribe(channel, type, cb)
    {
        var type = zcmtypes[type];
        var sub = zcmServer.subscribe(channel, function(channel, data) {
            cb(channel, type.decode(data));
        });
        return sub;
    }

    /**
     * Subscribes to all zcm channels on the created transport
     * @param {dispatchDecodedCallback} cb - callback to handle received messages
     * @returns {subscriptionRef} reference to the subscription, used to unsubscribe
     */
    function subscribe_all(cb)
    {
        return zcmServer.subscribe(".*", function(channel, data){
            var hash = ref.readInt64BE(data, 0);
            // RRR: as a courtesy to the user, we should make the nodejs zcmgen add a
            //      "typename" field to all zcmtypes that contains the string name of the
            //      object. This will let the subscriber have a little better knowledge of
            //      what decoded data they are actually getting back. Even though the channel
            //      is some hint, it would still be useful for things like spy
            cb(channel, zcmtypeHashMap[hash].decode(data));
        });
    }

    /**
     * Unsubscribes from the zcm channel referenced by the given subscription
     * @param {subscriptionRef} subscription - ref to the subscription to be unsubscribed from
     */
    function unsubscribe(subscription)
    {
        zcmServer.unsubscribe(subscription);
    }

    return {
        publish:        publish,
        subscribe:      subscribe,
        subscribe_all:  subscribe_all,
        unsubscribe:    unsubscribe,
    };
}

function zcm_create(zcmtypes, zcmurl, http)
{
    zcmurl = zcmurl || "ipc";

    var zcmServer = libzcmTransport(zcmurl);
    var ret = zcm(zcmtypes, zcmServer);

    if (http) {
        var io = require('socket.io')(http);

        io.on('connection', function(socket) {
            var subscriptions = {};
            var nextSub = 0;
            socket.on('client-to-server', function(data) {
                ret.publish(data.channel, data.type, data.msg);
            });
            socket.on('subscribe', function(data, returnSubscription) {
                var subId = data.subId;
                // RRR: switching over from the ffi interface to the slightly higher level
                //      subscribe function means the signature changes:
                var subscription = ret.subscribe(data.channel, data.type, function(channel, msg) {
                    socket.emit('server-to-client', {
                        channel: channel,
                        msg: msg,
                        id: subId
                    });
                });
                // RRR: this is what it was, delete it
                //var subscription = ret.subscribe(data.channel, function(channel, data) {
                    //socket.emit('server-to-client', {
                        //channel: channel,
                        //msg: subType.decode(data),
                        //id: subId
                    //});
                //});
                subscriptions[nextSub] = subscription;
                returnSubscription(nextSub++);
            });
            socket.on('subscribe_all', function(data, returnSubscription){
                var subId = data.subId;
                var subscription = ret.subscribe_all(function(channel, msg){
                    socket.emit('server-to-client', {
                        channel: channel,
                        msg: msg,
                        id: subId
                    });
                });
                subscriptions[nextSub] = subscription;
                returnSubscription(nextSub++);
            });
            socket.on('unsubscribe', function(subId) {
                ret.unsubscribe(subscriptions[subId]);
                delete subscriptions[subId];
            });
            socket.on('disconnect', function(){
                for (var id in subscriptions) {
                    ret.unsubscribe(subscriptions[id]);
                    delete subscriptions[id];
                }
                nextSub = 0;
            });
        });
    }

    // RRR: forgot this :)
    return ret;
}

exports.create = zcm_create;
