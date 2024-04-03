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

#include "sst_config.h"

#include "sst/core/link.h"

#include "sst/core/event.h"
#include "sst/core/factory.h"
#include "sst/core/initQueue.h"
#include "sst/core/pollingLinkQueue.h"
#include "sst/core/profile/eventHandlerProfileTool.h"
#include "sst/core/simulation_impl.h"
#include "sst/core/ssthandler.h"
#include "sst/core/timeConverter.h"
#include "sst/core/timeLord.h"
#include "sst/core/timeVortex.h"
#include "sst/core/uninitializedQueue.h"
#include "sst/core/unitAlgebra.h"

#include <utility>

namespace SST {

/**
 * Null Event.  Used when nullptr is passed into any of the send
 * functions.  On delivery, it will delete itself and return nullptr.
 */

class NullEvent : public Event
{
public:
    NullEvent() : Event() {}
    ~NullEvent() {}

    void execute(void) override
    {
        (*reinterpret_cast<HandlerBase*>(delivery_info))(nullptr);
        delete this;
    }

private:
    ImplementSerializable(SST::NullEvent)
};

//定义了一个LinkSendProfileToolList类用来管理一组工具
class LinkSendProfileToolList
{
public:
    LinkSendProfileToolList() {}
    //定义了一个eventSent方法，传递一个ev事件对象
    inline void eventSent(Event* ev)
    {
        for ( auto& x : tools ) {
            //调用tools字典中的每个元素，并对每个元素调用其first指向的对象的eventSent方法
            //传递ev和tools中的second元素
            x.first->eventSent(x.second, ev);
        }
    }
    
    void addProfileTool(SST::Profile::EventHandlerProfileTool* tool, const EventHandlerMetaData& mdata)
    {
        auto key = tool->registerHandler(mdata);
        tools.push_back(std::make_pair(tool, key));
    }

private:
    std::vector<std::pair<SST::Profile::EventHandlerProfileTool*, uintptr_t>> tools;
};

Link::Link(LinkId_t tag) :
    send_queue(nullptr),
    delivery_info(0),
    defaultTimeBase(0),
    latency(1),
    pair_link(nullptr),
    current_time(Simulation_impl::getSimulation()->currentSimCycle),
    type(UNINITIALIZED),
    mode(INIT),
    tag(tag),
    profile_tools(nullptr)
{}

Link::Link() :
    send_queue(nullptr),
    delivery_info(0),
    defaultTimeBase(0),
    latency(1),
    pair_link(nullptr),
    current_time(Simulation_impl::getSimulation()->currentSimCycle),
    type(UNINITIALIZED),
    mode(INIT),
    tag(-1),
    profile_tools(nullptr)
{}
//Link类的析构函数，这段代码是确保当Link对象被销毁时，与之关联的pair_link和profile_tools
//也被适当的清理，这可以防止资源泄露和未定义行为
Link::~Link()
{
    // Check to see if my pair_link is nullptr.  If not, let the other
    // link know I've been deleted
    //首先，析构函数检查 pair_link 指针是否不为 nullptr，并且确保它不指向当
    //前对象本身（pair_link != this）。这是因为 pair_link 通常用来指向与当
    //前 Link 对象配对的另一个 Link 对象，形成了一个双向链接。如果 pair_link 
    //存在且不是当前对象的地址，说明存在一个配对的链接。
    if ( pair_link != nullptr && pair_link != this ) {
        //向蒋配对链接的pair_link成员设置为nullptr，这样是为了断开之前的关联
        pair_link->pair_link = nullptr;
        // If my pair link is a SYNC link,
        // also need to delete it because no one else has a pointer to.
        //如果配对的链接是一个SYNC类型的链接（同步链接）
        if ( SYNC == pair_link->type ) delete pair_link;
    }

    if ( profile_tools ) delete profile_tools;
}
//此方法作用是在Link对象配对的过程中进行最后的设置和调整，这个方法会根据不同的
//Link类型执行不同的操作，确保Link对象正确地完成配置
void
Link::finalizeConfiguration()
{   
    //配置模式为RUN，表示Link对象已完成了初始化的过程，现在处于运行状态
    mode = RUN;
    if ( SYNC == type ) {
        // No configuration changes to be made
        return;
    }
    
    // If we have a queue, it means we ended up having init events
    // sent.  No need to keep the initQueue around
    //处理具有队列的链接，如果pair_link的send_queue成员变量为nullptr,表示没有初始化
    //事件被发送，因此不需要队列，在这种情况下，如果nullptr存在，就删除它并将其设置为
    //nullptr。
    if ( nullptr == pair_link->send_queue ) {
        delete pair_link->send_queue;
        pair_link->send_queue = nullptr;
    }
    //如果Link对象的type是HANDLER，那么pair_link的send_queue被设置为Simulation_imple
    //::getSimulation()->getTimeVortex()返回的队列，这是一个基于时间的队列，用于处理事件
    if ( HANDLER == type ) { pair_link->send_queue = Simulation_impl::getSimulation()->getTimeVortex(); }
    //如果type为POLL，则 pair_link 的 send_queue 被设置为一个新的 PollingLinkQueue 对象。
    //这是一个轮询队列，用于处理轮询的事件
    else if ( POLL == type ) {
        pair_link->send_queue = new PollingLinkQueue();
    }

    // If my pair link is a SYNC link, also need to call
    // finalizeConfiguration() on it since no one else has a pointer
    // to it
    //入伏哦pair_link的type也是SYNC,则通过调用本方法来实现配置
    if ( SYNC == pair_link->type ) pair_link->finalizeConfiguration();
}

//在Link对象准备完成其操作并进入完成状态前必要的清理和准备工作，这个方法主要关注的
//是Link对象的send_queue队列的处理，以及确保配对的Link对象也进行相同的准备工作
void
Link::prepareForComplete()
{
    //将Link对象的mode成员变量设置为COMPLETE,这表示Link对象正在进入完成状态
    mode = COMPLETE;

    //处理SYNC类型的链接，如果Link对象的type成员变量等于SYNC，表示这是一个同步链接
    //对于同步链接，在准备完成时不需要进行任何配置的修改
    if ( SYNC == type ) {
        // No configuration changes to be made
        return;
    }

    //如果Link对象的type是POLL，表示这是一个轮询类型的链接，在这种情况下，需要删除
    //pair_link->send_queue指向的队列，然后将其设置为nullptr
    if ( POLL == type ) { delete pair_link->send_queue; }

    pair_link->send_queue = nullptr;

    // If my pair link is a SYNC link, also need to call
    // prepareForComplete() on it
    //如果pair_link对象的type也是SYNC，表示其也是同步的，由于没有其他指针指向这个
    //SYNC类型的链接，需要调用本方法来确保它进行准备完成的工作
    if ( SYNC == pair_link->type ) pair_link->prepareForComplete();
}

void
Link::setPolling()
{
    type = POLL;
}


void
Link::setLatency(Cycle_t lat)
{
    latency = lat;
}
//增加本链接对象的链接接收延迟
void
Link::addSendLatency(int cycles, const std::string& timebase)
{
    //调用getSimulation()->getTimeLord()->getSimCycles方法，将字符串形式的时间基准转换为
    //模拟周期数tb。然后，他将cycles乘以转换后的tb，得到增加的延迟量，并将它累加到Link对象
    //的latency成员变量上，这种链式函数调用是c++中常用的写法，它使得代码更加简洁。
    SimTime_t tb = Simulation_impl::getSimulation()->getTimeLord()->getSimCycles(timebase, "addOutputLatency");
    latency += (cycles * tb);
}

void
Link::addSendLatency(SimTime_t cycles, TimeConverter* timebase)
{
    //调用timebase的convertToCoreTime方法，将cycles转换为模拟核心时间。然后累加到Link对象的latency
    //成员变量上
    latency += timebase->convertToCoreTime(cycles);
}

//增加配对链接的链接接收延迟
void
Link::addRecvLatency(int cycles, const std::string& timebase)
{
    SimTime_t tb = Simulation_impl::getSimulation()->getTimeLord()->getSimCycles(timebase, "addOutputLatency");
    pair_link->latency += (cycles * tb);
}

void
Link::addRecvLatency(SimTime_t cycles, TimeConverter* timebase)
{
    pair_link->latency += timebase->convertToCoreTime(cycles);
}

//此方法用于设置与Link对象关联的时间处理器
void
Link::setFunctor(Event::HandlerBase* functor)
{
    //如果Link对象的type为POLL类型，表示这是一个轮询类型的链接
    if ( UNLIKELY(type == POLL) ) {
        //如果是，就不该调用setFunctor函数，他会输出一个错误信息并终止程序
        Simulation_impl::getSimulation()->getSimulationOutput().fatal(
            CALL_INFO, 1, "Cannot call setFunctor on a Polling Link\n");
    }
//如果不是，函数就将type设置为HADNLER，表示链接现在将使用事件处理器
    type                     = HANDLER;
    //函数将传入事件处理器指针functor存储在配对链接(pair_link)的delivery_info成员变量中
    //这是通过将指针转换为uintptr_t类型并存储实现的
    pair_link->delivery_info = reinterpret_cast<uintptr_t>(functor);
}

void
Link::replaceFunctor(Event::HandlerBase* functor)
{
    if ( UNLIKELY(type == POLL) ) {
        Simulation_impl::getSimulation()->getSimulationOutput().fatal(
            CALL_INFO, 1, "Cannot call replaceFunctor on a Polling Link\n");
    }

    type = HANDLER;
    //检查pair_link链接的delivery_info成员变量是否包含一个有效的事件处理器指针
    if ( pair_link->delivery_info ) {
        //如果存在旧的处理器指针，函数首先将其转换回HandlerBase*类型的指针
        auto* handler = reinterpret_cast<Event::HandlerBase*>(pair_link->delivery_info);
        //调用新处理器的transferProfilingInfo方法将就处理器的分析信息传递给新处理器
        functor->transferProfilingInfo(handler);
        //之后释放掉旧处理器的资源
        delete handler;
    }
    //最后将传入的新事件处理器指针functor传入配对链接的delivery_info成员变量中
    pair_link->delivery_info = reinterpret_cast<uintptr_t>(functor);
}

//在模拟环境中发送一个事件，delay表示发送延迟，event表示要发送的事件对象
void
Link::send_impl(SimTime_t delay, Event* event)
{
    //如果Link对象的mode成员变量不为RUN
    if ( RUN != mode ) {
        //如果mode的值为INIT，表示尝试在初始化阶段发送或接收
        if ( INIT == mode ) {
            Simulation_impl::getSimulation()->getSimulationOutput().fatal(
                CALL_INFO, 1,
                "ERROR: Trying to send or recv from link during initialization.  Send and Recv cannot be called before "
                "setup.\n");
        }
        else if ( COMPLETE == mode ) {
            Simulation_impl::getSimulation()->getSimulationOutput().fatal(
                CALL_INFO, 1, "ERROR: Trying to call send or recv during complete phase.");
        }
    }
    //计算事件的发送时延
    Cycle_t cycle = current_time + delay + latency;
    //如果没有提供事件对象，方法会创建一个新的NullEvent对象
    if ( event == nullptr ) { event = new NullEvent(); }
    event->setDeliveryTime(cycle);
    event->setDeliveryInfo(tag, delivery_info);
//如果编译时定义了_SST_DEBUG_EVENT_TRACKING_,这意味着在调试模式下，事件跟踪功能被启用
#if __SST_DEBUG_EVENT_TRACKING__
    event->addSendComponent(comp, ctype, port);
    event->addRecvComponent(pair_link->comp, pair_link->ctype, pair_link->port);
#endif

    if ( profile_tools ) profile_tools->eventSent(event);
    send_queue->insert(event);
}

//定义了Link类的recv方法
Event*
Link::recv()
{
    // Check to make sure this is a polling link
    //检查Link对象的type是否为轮询类型的链接，若不是则不应该在这个链接上调用recv方法
    if ( UNLIKELY(type != POLL) ) {
        Simulation_impl::getSimulation()->getSimulationOutput().fatal(
            CALL_INFO, 1, "Cannot call recv on a Link with an event handler installed (non-polling link.\n");
    }
    //定义一个Event*类型的指针event并初始化为nullptr，将用于存储接收到的事件对象
    Event*      event      = nullptr;
    //方法获取当前的模拟对象的指针
    Simulation* simulation = Simulation_impl::getSimulation();
    //方法检查配对的链接send_queue是否为空。它是一个队列，存储了要发送给当前链接的事件
    if ( !pair_link->send_queue->empty() ) {
        //若不为空，方法获取队列的前端事件activity
        Activity* activity = pair_link->send_queue->front();
        //检查activity的发送事件getDeliveryTime是否小于或等于模拟对象的当前模拟周期
        if ( activity->getDeliveryTime() <= simulation->getCurrentSimCycle() ) {
            //将activity事件强转为Event*类型，并将其地址赋给event指针
            event = static_cast<Event*>(activity);
            //从send_queue中移除activity事件，表示该事件已被处理
            pair_link->send_queue->pop();
        }
    }
    return event;
}

void
Link::sendUntimedData(Event* data)
{
    //检查Link对象的mode成员变量是否为RUN
    if ( RUN == mode ) {
        //不能在运行期间调用此方法，因为未定时的数据通常在初始化阶段进行
        Simulation_impl::getSimulation()->getSimulationOutput().fatal(
            CALL_INFO, 1,
            "ERROR: Trying to call sendUntimedData/sendInitData or recvUntimedData/recvInitData during the run phase.");
    }
    //如果Link对象的send_queue成员变量为空，表示还没初始化发送队列，会创建一个新的
    //InitQueue对象并将其赋值给send_queue
    if ( send_queue == nullptr ) { send_queue = new InitQueue(); }
    //通过getSimulation方法获取当前的模拟对象，并增加其untimed_msg_count计数器，用于跟踪为定时
    //消息的数量，并增加其untimed_msg_count计数器，用于跟踪未定时消息的数量
    Simulation_impl::getSimulation()->untimed_msg_count++;
    //为data事件设置一个发送时间，通常是untimed_phase+1
    data->setDeliveryTime(Simulation_impl::getSimulation()->untimed_phase + 1);
    //设置事件的发送消息，包括tag和delivery_info
    data->setDeliveryInfo(tag, delivery_info);
    //将data事件插入到send_queue队列中，以便在模拟的适当阶段进行处理
    send_queue->insert(data);
//如果编译时定义了 __SST_DEBUG_EVENT_TRACKING__，这通常意味着在调试模式下，事件
//跟踪功能被启用。在这种情况下，方法会调用data->addSendComponent和data->addRecvComponent
//方法来记录发送和接收事件的组件消息。这些信息包括组件的名称、类型和端口
#if __SST_DEBUG_EVENT_TRACKING__
    data->addSendComponent(comp, ctype, port);
    data->addRecvComponent(pair_link->comp, pair_link->ctype, pair_link->port);
#endif
}

// Called by SyncManager
//未定时的数据通常在模拟的初始化阶段使用，而不是在正式的运行阶段
void
Link::sendUntimedData_sync(Event* data)
{
    //检查Link对象的send_queue是否为空，若是则创建一个新的InitQueue对象并将其
    //赋值给send_queue.这个队列用于存储在初始化阶段发送未定时的数据
    if ( send_queue == nullptr ) { send_queue = new InitQueue(); }

    send_queue->insert(data);
}

Event*
Link::recvUntimedData()
{
    //这个方法用于检查配对的链接的send_queue是否为空，若是则表示没有未定时数据
    //可接收，返回nullptr
    if ( pair_link->send_queue == nullptr ) return nullptr;

    //如不为空，则获取前端事件activity
    Event* event = nullptr;
    if ( !pair_link->send_queue->empty() ) {
        Activity* activity = pair_link->send_queue->front();
        //检查getDeliveryTime是否小于或等于当前模拟对象的untimed_phase,若事件时间已到
        //或者过期，表示事件可以被接收
        if ( activity->getDeliveryTime() <= Simulation_impl::getSimulation()->untimed_phase ) {
            //将Activity*类型转换为Event*类型
            event = static_cast<Event*>(activity);
            //从队列中移除该事件，表示事件已被处理
            pair_link->send_queue->pop();
        }
    }
    //返回一个event对象，他要么为nullptr，要么指向一个有效的Event对象
    return event;
}
//设置Link对象关联的默认时间基准转换器，他的作用通常是将事件或者模拟活动的时间从
//一个单位转换到另一个单位，例如从纳秒转换到时钟周期
void
Link::setDefaultTimeBase(TimeConverter* tc)//传入时间转换器的指针
{
    //判断是否为nullptr
    if ( tc == nullptr )
        //若是空指针，表示没有提供时间转换器，因此Link对象的defaultTimeBase成员变量
        //被设置为0
        defaultTimeBase = 0;
    else
        //若不为空则调用getFactor获取时间转换器的转换因子，并将其赋值给Link对象的
        //defaultTimeBase成员变量
        defaultTimeBase = tc->getFactor();
}

TimeConverter*
Link::getDefaultTimeBase()
{
    if ( defaultTimeBase == 0 ) return nullptr;
    return Simulation_impl::getSimulation()->getTimeLord()->getTimeConverter(defaultTimeBase);
}
//他的值在声明之后不能被修改，它可以提高数据的安全性，一次声明后，无法在修改数据的值
const TimeConverter*
Link::getDefaultTimeBase() const
{
    if ( defaultTimeBase == 0 ) return nullptr;
    return Simulation_impl::getSimulation()->getTimeLord()->getTimeConverter(defaultTimeBase);
}

//这个方法用于在模拟中收集和分析事件处理的相关信息
void
Link::addProfileTool(SST::Profile::EventHandlerProfileTool* tool, const EventHandlerMetaData& mdata)
{
    //若profile_tools是nullptr，表示Link对象尚未拥有管理虚拟分析工具的列表，因此需要
    //创建一个新的LinkSendProfileToolList对象并将其赋值给profile_tools
    if ( !profile_tools ) profile_tools = new LinkSendProfileToolList();
    //若已经初始化，则调用addProfileTool方法，使得Link对象能够添加性能分析工具
    //以便在模拟过程中收集和分析事件处理的性能数据
    profile_tools->addProfileTool(tool, mdata);
}


} // namespace SST
