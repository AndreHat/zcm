// DO NOT EVER INCLUDE THIS HEADER FILE YOURSELF

#ifndef __zcm_cpp_impl_ok__
#error "Don't include this file"
#endif

#ifdef ZCM_EMBEDDED
#define nullptr NULL
#endif

#include <stdlib.h>

// =============== implementation ===============

// Note: To prevent compiler "redefinition" issues, all functions in this file must be declared
//       as `inline`

#ifndef ZCM_EMBEDDED
inline ZCM::ZCM()
{
    zcm = zcm_create(nullptr);
}
#endif

#ifndef ZCM_EMBEDDED
inline ZCM::ZCM(const zstring_t& transport)
{
    zcm = zcm_create(transport.c_str());
}
#endif

inline ZCM::ZCM(zcm_trans_t* zt)
{
    zcm = (zcm_t*) malloc(sizeof(zcm_t));
    zcm_init_trans(zcm, zt);
}

inline ZCM::~ZCM()
{
    if (zcm != nullptr)
        zcm_destroy(zcm);

    std::vector<Subscription*>::iterator end = subscriptions.end(),
                                          it = subscriptions.begin();
    for (;it != end; ++it) delete *it;

    zcm = nullptr;
}

inline zbool_t ZCM::good() const
{
    return zcm != nullptr && err() == ZCM_EOK;
}

inline zcm_retcode_t ZCM::err() const
{
    return zcm_errno(zcm);
}

inline const zchar_t* ZCM::strerror() const
{
    return zcm_strerror(zcm);
}

inline const zchar_t* ZCM::strerrno(zcm_retcode_t err) const
{
    return zcm_strerrno(err);
}

#ifndef ZCM_EMBEDDED
inline void ZCM::run()
{
    return zcm_run(zcm);
}
#endif

#ifndef ZCM_EMBEDDED
inline void ZCM::start()
{
    return zcm_start(zcm);
}
#endif

#ifndef ZCM_EMBEDDED
inline void ZCM::stop()
{
    return zcm_stop(zcm);
}
#endif

#ifndef ZCM_EMBEDDED
inline void ZCM::pause()
{
    return zcm_pause(zcm);
}
#endif

#ifndef ZCM_EMBEDDED
inline void ZCM::resume()
{
    return zcm_resume(zcm);
}
#endif

#ifndef ZCM_EMBEDDED
inline zbool_t ZCM::handle()
{
    return zcm_handle(zcm);
}
#endif

#ifndef ZCM_EMBEDDED
inline void ZCM::setQueueSize(zuint32_t sz)
{
    return zcm_set_queue_size(zcm, sz);
}
#endif

inline zbool_t ZCM::handleNonblock()
{
    return zcm_handle_nonblock(zcm);
}

inline void ZCM::flush()
{
    return zcm_flush(zcm);
}

inline zbool_t ZCM::publish(const zstring_t& channel, const zuint8_t* data, zuint32_t len)
{
    return publishRaw(channel, data, len);
}

template <class Msg>
inline zbool_t ZCM::publish(const zstring_t& channel, const Msg* msg)
{
    zuint32_t len = msg->getEncodedSize();
    zuint8_t* buf = new zuint8_t[len];
    if (!buf) {
        zcm->err = ZCM_EMEMORY;
        return zfalse;
    }
    zint32_t encodeRet = msg->encode(buf, 0, len);
    if (encodeRet < 0 || (zuint32_t) encodeRet != len) {
        delete[] buf;
        zcm->err = ZCM_EAGAIN;
        return zfalse;
    }
    auto ret = publishRaw(channel, buf, len);
    delete[] buf;
    return ret;
}

inline Subscription* ZCM::subscribe(const zstring_t& channel,
                                    void (*cb)(const ReceiveBuffer* rbuf,
                                               const zstring_t& channel, void* usr),
                                    void* usr)
{
    if (!zcm) {
        #ifndef ZCM_EMBEDDED
        fprintf(stderr, "ZCM instance not initialized.  Ignoring call to subscribe()\n");
        #endif
        return nullptr;
    }

    typedef Subscription SubType;
    SubType* sub = new SubType();
    ZCM_ASSERT(sub);
    sub->usr = usr;
    sub->callback = cb;
    subscribeRaw(sub->rawSub, channel, SubType::dispatch, sub);

    subscriptions.push_back(sub);
    return sub;
}

// Virtual inheritance to avoid ambiguous base class problem http://stackoverflow.com/a/139329
template<class Msg>
class TypedSubscription : public virtual Subscription
{
    friend class ZCM;

  protected:
    void (*typedCallback)(const ReceiveBuffer* rbuf, const zstring_t& channel, const Msg* msg,
                          void* usr);
    Msg msgMem; // Memory to decode this message into

  public:
    virtual ~TypedSubscription() {}

    inline zbool_t readMsg(const ReceiveBuffer* rbuf)
    {
        zint32_t status = msgMem.decode(rbuf->data, 0, rbuf->data_size);
        if (status < 0) {
            #ifndef ZCM_EMBEDDED
            fprintf (stderr, "error %d decoding %s!!!\n", status, Msg::getTypeName());
            #endif
            return zfalse;
        }
        return ztrue;
    }

    inline void typedDispatch(const ReceiveBuffer* rbuf, const zstring_t& channel)
    {
        if (!readMsg(rbuf)) return;
        (*typedCallback)(rbuf, channel, &msgMem, usr);
    }

    static inline void dispatch(const ReceiveBuffer* rbuf, const zchar_t* channel, void* usr)
    {
        ((TypedSubscription<Msg>*)usr)->typedDispatch(rbuf, channel);
    }
};

#if __cplusplus > 199711L
// Virtual inheritance to avoid ambiguous base class problem http://stackoverflow.com/a/139329
template<class Msg>
class TypedFunctionalSubscription : public virtual Subscription
{
    friend class ZCM;

  protected:
    std::function<void (const ReceiveBuffer* rbuf,
                        const zstring_t& channel,
                        const Msg* msg)> cb;
    Msg msgMem; // Memory to decode this message into

  public:
    virtual ~TypedFunctionalSubscription() {}

    inline zbool_t readMsg(const ReceiveBuffer* rbuf)
    {
        zint32_t status = msgMem.decode(rbuf->data, 0, rbuf->data_size);
        if (status < 0) {
            #ifndef ZCM_EMBEDDED
            fprintf (stderr, "error %d decoding %s!!!\n", status, Msg::getTypeName());
            #endif
            return zfalse;
        }
        return ztrue;
    }

    inline void typedDispatch(const ReceiveBuffer* rbuf, const zstring_t& channel)
    {
        if (!readMsg(rbuf)) return;
        cb(rbuf, channel, &msgMem);
    }

    static inline void dispatch(const ReceiveBuffer* rbuf, const zchar_t* channel, void* usr)
    {
        ((TypedFunctionalSubscription<Msg>*)usr)->typedDispatch(rbuf, channel);
    }
};
#endif

// Virtual inheritance to avoid ambiguous base class problem http://stackoverflow.com/a/139329
template <class Handler>
class HandlerSubscription : public virtual Subscription
{
    friend class ZCM;

  protected:
    Handler* handler;
    void (Handler::*handlerCallback)(const ReceiveBuffer* rbuf, const zstring_t& channel);

  public:
    virtual ~HandlerSubscription() {}

    inline void handlerDispatch(const ReceiveBuffer* rbuf, const zstring_t& channel)
    {
        (handler->*handlerCallback)(rbuf, channel);
    }

    static inline void dispatch(const ReceiveBuffer* rbuf, const zchar_t* channel, void* usr)
    {
        ((HandlerSubscription<Handler>*)usr)->handlerDispatch(rbuf, channel);
    }
};

template <class Msg, class Handler>
class TypedHandlerSubscription : public TypedSubscription<Msg>, HandlerSubscription<Handler>
{
    friend class ZCM;

  protected:
    void (Handler::*typedHandlerCallback)(const ReceiveBuffer* rbuf,
                                          const zstring_t& channel,
                                          const Msg* msg);

  public:
    virtual ~TypedHandlerSubscription() {}

    inline void typedHandlerDispatch(const ReceiveBuffer* rbuf, const zstring_t& channel)
    {
        // Unfortunately, we need to add "this" here to handle template inheritance:
        // https://isocpp.org/wiki/faq/templates#nondependent-name-lookup-members
        if (!this->readMsg(rbuf)) return;
        (this->handler->*typedHandlerCallback)(rbuf, channel, &this->msgMem);
    }

    static inline void dispatch(const ReceiveBuffer* rbuf, const char* channel, void* usr)
    {
        ((TypedHandlerSubscription<Msg, Handler>*)usr)->typedHandlerDispatch(rbuf, channel);
    }
};

// TODO: lots of room to condense the implementations of the various subscribe functions
template <class Msg, class Handler>
inline Subscription* ZCM::subscribe(const zstring_t& channel,
                                    void (Handler::*cb)(const ReceiveBuffer* rbuf,
                                                        const zstring_t& channel,
                                                        const Msg* msg),
                                    Handler* handler)
{
    if (!zcm) {
        #ifndef ZCM_EMBEDDED
        fprintf(stderr, "ZCM instance not initialized. Ignoring call to subscribe()\n");
        #endif
        return nullptr;
    }

    typedef TypedHandlerSubscription<Msg, Handler> SubType;
    SubType* sub = new SubType();
    ZCM_ASSERT(sub);
    sub->handler = handler;
    sub->typedHandlerCallback = cb;
    subscribeRaw(sub->rawSub, channel, SubType::dispatch, sub);

    subscriptions.push_back(sub);
    return sub;
}

template <class Handler>
inline Subscription* ZCM::subscribe(const zstring_t& channel,
                                    void (Handler::*cb)(const ReceiveBuffer* rbuf,
                                                        const zstring_t& channel),
                                    Handler* handler)
{
    if (!zcm) {
        #ifndef ZCM_EMBEDDED
        fprintf(stderr, "ZCM instance not initialized.  Ignoring call to subscribe()\n");
        #endif
        return nullptr;
    }

    typedef HandlerSubscription<Handler> SubType;
    SubType* sub = new SubType();
    ZCM_ASSERT(sub);
    sub->handler = handler;
    sub->handlerCallback = cb;
    subscribeRaw(sub->rawSub, channel, SubType::dispatch, sub);

    subscriptions.push_back(sub);
    return sub;
}

template <class Msg>
inline Subscription* ZCM::subscribe(const zstring_t& channel,
                                    void (*cb)(const ReceiveBuffer* rbuf,
                                               const zstring_t& channel,
                                               const Msg* msg, void* usr),
                                    void* usr)
{
    if (!zcm) {
        #ifndef ZCM_EMBEDDED
        fprintf(stderr, "ZCM instance not initialized.  Ignoring call to subscribe()\n");
        #endif
        return nullptr;
    }

    typedef TypedSubscription<Msg> SubType;
    SubType* sub = new SubType();
    ZCM_ASSERT(sub);
    sub->usr = usr;
    sub->typedCallback = cb;
    subscribeRaw(sub->rawSub, channel, SubType::dispatch, sub);

    subscriptions.push_back(sub);
    return sub;
}

#if __cplusplus > 199711L
template <class Msg>
inline Subscription* ZCM::subscribe(const zstring_t& channel,
                                    std::function<void (const ReceiveBuffer* rbuf,
                                                        const zstring_t& channel,
                                                        const Msg* msg)> cb)
{
    if (!zcm) {
        #ifndef ZCM_EMBEDDED
        fprintf(stderr, "ZCM instance not initialized. Ignoring call to subscribe()\n");
        #endif
        return nullptr;
    }

    typedef TypedFunctionalSubscription<Msg> SubType;
    SubType* sub = new SubType();
    ZCM_ASSERT(sub);
    sub->usr = nullptr;
    sub->cb = cb;
    subscribeRaw(sub->rawSub, channel, SubType::dispatch, sub);

    subscriptions.push_back(sub);
    return sub;
}
#endif

inline void ZCM::unsubscribe(Subscription* sub)
{
    std::vector<Subscription*>::iterator end = subscriptions.end(),
                                          it = subscriptions.begin();
    for (; it != end; ++it) {
        if (*it == sub) {
            unsubscribeRaw(sub->rawSub);
            subscriptions.erase(it);
            delete sub;
            break;
        }
    }
}

inline zcm_t* ZCM::getUnderlyingZCM()
{ return zcm; }

inline zbool_t ZCM::publishRaw(const zstring_t& channel, const zuint8_t* data, zuint32_t len)
{ return zcm_publish(zcm, channel.c_str(), data, len); }

inline void ZCM::subscribeRaw(void*& rawSub, const zstring_t& channel,
                              MsgHandler cb, void* usr)
{ rawSub = zcm_subscribe(zcm, channel.c_str(), cb, usr); }

inline void ZCM::unsubscribeRaw(void*& rawSub)
{ zcm_unsubscribe(zcm, (zcm_sub_t*) rawSub); rawSub = nullptr; }


// ***********************************
// LogFile and LogEvent implementation
// ***********************************
#ifndef ZCM_EMBEDDED
inline LogFile::LogFile(const zstring_t& path, const zstring_t& mode)
{
    this->eventlog = zcm_eventlog_create(path.c_str(), mode.c_str());
    this->lastevent = nullptr;
}

inline void LogFile::close()
{
    if (eventlog)
        zcm_eventlog_destroy(eventlog);
    eventlog = nullptr;
    if(lastevent)
        zcm_eventlog_free_event(lastevent);
    lastevent = nullptr;
}

inline LogFile::~LogFile()
{
    close();
}

inline zbool_t LogFile::good() const
{
    return eventlog != nullptr;
}

inline zcm_retcode_t LogFile::seekToTimestamp(zuint64_t timestamp)
{
    return zcm_eventlog_seek_to_timestamp(eventlog, timestamp);
}

inline FILE* LogFile::getFilePtr()
{
    return zcm_eventlog_get_fileptr(eventlog);
}

inline const LogEvent* LogFile::cplusplusIfyEvent(zcm_eventlog_event_t* evt)
{
    if (lastevent) zcm_eventlog_free_event(lastevent);
    lastevent = evt;
    if (!evt) return nullptr;
    curEvent.eventnum = evt->eventnum;
    curEvent.channel.assign(evt->channel, evt->channellen);
    curEvent.timestamp = evt->timestamp;
    curEvent.datalen = evt->datalen;
    curEvent.data = evt->data;
    return &curEvent;
}

inline const LogEvent* LogFile::readNextEvent()
{
    zcm_eventlog_event_t* evt = zcm_eventlog_read_next_event(eventlog);
    return cplusplusIfyEvent(evt);
}

inline const LogEvent* LogFile::readPrevEvent()
{
    zcm_eventlog_event_t* evt = zcm_eventlog_read_prev_event(eventlog);
    return cplusplusIfyEvent(evt);
}

inline const LogEvent* LogFile::readEventAtOffset(zoff_t offset)
{
    zcm_eventlog_event_t* evt = zcm_eventlog_read_event_at_offset(eventlog, offset);
    return cplusplusIfyEvent(evt);
}

inline zbool_t LogFile::writeEvent(const LogEvent* event)
{
    zcm_eventlog_event_t evt;
    evt.eventnum = event->eventnum;
    evt.timestamp = event->timestamp;
    evt.channellen = event->channel.size();
    evt.datalen = event->datalen;
    // casting away constness okay because evt isn't used past the end of this function
    evt.channel = (zchar_t*) event->channel.c_str();
    evt.data = event->data;
    return zcm_eventlog_write_event(eventlog, &evt);
}
#endif
