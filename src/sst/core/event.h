// Copyright 2009-2023 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2023, NTESS
// All rights reserved.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef SST_CORE_EVENT_H
#define SST_CORE_EVENT_H

#include "sst/core/activity.h"
#include "sst/core/sst_types.h"
#include "sst/core/ssthandler.h"

#include <atomic>
#include <cinttypes>
#include <string>
//SST是一个命名空间，用于组织和封装相关的类和函数，这有助于避免名称冲突
//并提供一个清晰的代码结构
namespace SST {

class Link;
class NullEvent;
class RankSync;
class ThreadSync;

/**
 * Base class for Events - Items sent across links to communicate between
 * components.
 */
//Event类是Activity的派生类，意味着它添加了新的成员或重写了一些函数
class Event : public Activity
{
public:
    /**
       Base handler for event delivery.
     */
    //定义了一个类型别名HandlerBase，它指向SST的一个事件处理程序的基类，它是一个模板类
    //用于处理事件传递时的回调函数
    using HandlerBase = SSTHandlerBase<void, Event*>;

    //处理器是一个回调函数，当事件被传递时会被调用，有两种形式的处理器创建
    /**
       Used to create handlers for event delivery.  The callback
       function is expected to be in the form of:
         //对于不涉及静态数据的事件处理器
         void func(Event* event)

       In which case, the class is created with:

         new Event::Handler<classname>(this, &classname::function_name)

       Or, to add static data, the callback function is:
         //如果回调函数需要额外的静态数据
         void func(Event* event, dataT data)

       and the class is created with:

         new Event::Handler<classname, dataT>(this, &classname::function_name, data)
     */
    template <typename classT, typename dataT = void>
    //定义了Handler作为SSTHandler的别名，SSTHandler是一个模板类，用于创建事件处理器
    using Handler = SSTHandler<void, Event*, classT, dataT>;

    /** Type definition of unique identifiers */
    //唯一标识符类型定义id_type,uint64_t和int分别用于存储事件唯一标识符的
    //高64位和低32位
    typedef std::pair<uint64_t, int> id_type;
    /** Constant, default value for id_types */
    //常量NO_ID,用于表示一个无效或未设置的事件标识符，它是id_type类型的默认构造值
    static const id_type             NO_ID;
    //Event类的构造函数首先是调用Activity的构造函数
    Event() : Activity(), delivery_info(0)
    {   
        //设置事件的默认优先级位EVENTPRIORITY(值为50，优先级中等)
        setPriority(EVENTPRIORITY);
//如果预处理宏__SST_DEBUG_EVENT_TRACKING__ 被定义，构造函数还会初始化
//first_comp 和 last_comp 成员变量为一个空字符串。这些变量用于调试目的，
//以跟踪事件的传播。
#if __SST_DEBUG_EVENT_TRACKING__
        first_comp = "";
        last_comp  = "";
#endif
    }
    virtual ~Event();

    /** Clones the event in for the case of a broadcast */
    virtual Event* clone();


#ifdef __SST_DEBUG_EVENT_TRACKING__
    //这个函数用于输出事件的跟踪信息
    virtual void printTrackingInfo(const std::string& header, Output& out) const override
    {
        //函数使用out.output方法格式化并输出事件的跟踪信息，包括事件首次发送的组件名称
        //端口和类型，以及最后接收的组件名称、端口和类型
        out.output(
            "%s Event first sent from: %s:%s (type: %s) and last received by %s:%s (type: %s)\n", header.c_str(),
            first_comp.c_str(), first_port.c_str(), first_type.c_str(), last_comp.c_str(), last_port.c_str(),
            last_type.c_str());
    }
    //获取组件信息的函数，这些函数用于获取事件首次发送和最后接收的组件的名称、类型和端口
    //它们返回对应的字符串引用
    const std::string& getFirstComponentName() { return first_comp; }
    const std::string& getFirstComponentType() { return first_type; }
    const std::string& getFirstPort() { return first_port; }
    const std::string& getLastComponentName() { return last_comp; }
    const std::string& getLastComponentType() { return last_type; }
    const std::string& getLastPort() { return last_port; }
    //添加发送和接收组件信息的函数
    void addSendComponent(const std::string& comp, const std::string& type, const std::string& port)
    {
        //如果first_comp还没有被设置，他将会传入组件的名称、类型和端口保存起来
        if ( first_comp == "" ) {
            first_comp = comp;
            first_type = type;
            first_port = port;
        }
    } 
    //用于添加事件最后接收的组件信息，他会直接更新last_comp、last_type和last_port成员变量
    //为传入的组件名称、类型和端口
    void addRecvComponent(const std::string& comp, const std::string& type, const std::string& port)
    {
        last_comp = comp;
        last_type = type;
        last_port = port;
    }

#endif
    //重写了基类Activity种的serialize_order方法，用于序列化Rvent对象的特定数据
    void serialize_order(SST::Core::Serialization::serializer& ser) override
    {
        //首先，序列化从Activity类继承的成员变量
        Activity::serialize_order(ser);
        ser& delivery_info;
//如果定义了预处理宏，则还会序列化一系列额外的成员变量
#ifdef __SST_DEBUG_EVENT_TRACKING__
        ser& first_comp;//首次发送事件的组件名称
        ser& first_type;//首次发送事件的组件类型
        ser& first_port;//首次发送事件组件端口
        ser& last_comp;//最后接收事件的组件名称
        ser& last_type;
        ser& last_port;
#endif
    }

protected:
    /**
     * Generates an ID that is unique across ranks, components and events.
     */
    //一个受保护的成员函数，生成一个在整个模拟中唯一的标识符，这个标识符跨越
    //不同的ranks、组件和事件
    id_type generateUniqueId();

//final用于声明一些其他类时Event类的朋友，其他类可以直接访问私有变量
private:
    friend class Link;
    friend class NullEvent;
    friend class RankSync;
    friend class ThreadSync;


    /** Cause this event to fire */
    //事件到达其预定的交付时间时触发事件的执行
    void execute(void) override;

    /**
       This sets the information needed to get the event properly
       delivered for the next step of transfer.

       The tag is used to deterministically sort the events and is
       based off of the sorted link names.  This field is unused for
       events sent across link connected to sync objects.

       For links that are going to a sync, the delivery_info is used
       on the remote side to send the event on the proper link.  For
       local links, delivery_info contains the delivery functor.
       @return void
     */
    //用于设置事件的传递信息
    inline void setDeliveryInfo(LinkId_t tag, uintptr_t delivery_info)
    {
        //用于设置事件的顺序标签或队列顺序
        setOrderTag(tag);
        this->delivery_info = delivery_info;
    }

    /** Gets the link id used for delivery.  For use by SST Core only */
    //获取用于事件传递的Link对象的指针
    inline Link* getDeliveryLink() { return reinterpret_cast<Link*>(delivery_info); }

    /** Gets the link id associated with this event.  For use by SST Core only */
    //用于获取与事件关联的传递标识符
    inline LinkId_t getTag(void) const { return getOrderTag(); }


    /** Holds the delivery information.  This is stored as a
      uintptr_t, but is actually a pointer converted using
      reinterpret_cast.  For events send on links connected to a
      Component/SubComponent, this holds a pointer to the delivery
      functor.  For events sent on links connected to a Sync object,
      this holds a pointer to the remote link to send the event on
      after synchronization.
    */
    uintptr_t delivery_info;

private:
    static std::atomic<uint64_t> id_counter;

#ifdef __SST_DEBUG_EVENT_TRACKING__
    std::string first_comp;
    std::string first_type;
    std::string first_port;
    std::string last_comp;
    std::string last_type;
    std::string last_port;
#endif

    ImplementVirtualSerializable(SST::Event)
};

/**
 * Empty Event.  Does nothing.
 */
//定义了一个空事件
class EmptyEvent : public Event
{
public:
    EmptyEvent() : Event() {}
    ~EmptyEvent() {}

private:
    //这个宏是用于实现序列化接口的，它表明EmptyEvent类或者其派生类
    //需要实现某些序列化相关的虚函数，以便将对象转换为可以存储或传输的形式
    ImplementSerializable(SST::EmptyEvent)
};
//用于存储事件处理器相关的元数据，这个类提供了一些额外的信息，如组件的ID、名称
//、类型和端口名称
class EventHandlerMetaData : public HandlerMetaData
{
public:
    const ComponentId_t comp_id;//用于存储组件的唯一标识符
    const std::string   comp_name;//一个字符串型变量，用于存储组件的名称
    const std::string   comp_type;//用于存储组件的类型
    const std::string   port_name;//用于存储组件的端口名称

    EventHandlerMetaData(
        ComponentId_t id, const std::string& cname, const std::string& ctype, const std::string& pname) :
        comp_id(id),
        comp_name(cname),
        comp_type(ctype),
        port_name(pname)
    {}

    ~EventHandlerMetaData() {}
};

} // namespace SST

#endif // SST_CORE_EVENT_H
