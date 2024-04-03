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

#ifndef SST_CORE_ACTIVITY_H
#define SST_CORE_ACTIVITY_H

#include "sst/core/mempool.h"
#include "sst/core/output.h"
#include "sst/core/serialization/serializable.h"
#include "sst/core/sst_types.h"
#include "sst/core/warnmacros.h"

#include <cinttypes>
#include <cstring>
#include <errno.h>
#include <sstream>
#include <unordered_map>

// Default Priority Settings
//这些宏用于配置系统中不同类型的操作或事件的优先级，在多线程或事件驱动的程序中
//优先级用于确定哪些事件或任务应该首先被处理
#define THREADSYNCPRIORITY     20
#define SYNCPRIORITY           25
#define STOPACTIONPRIORITY     30
#define CLOCKPRIORITY          40
#define EVENTPRIORITY          50
#define MEMEVENTPRIORITY       50
#define BARRIERPRIORITY        75
#define ONESHOTPRIORITY        80
#define STATISTICCLOCKPRIORITY 85
#define FINALEVENTPRIORITY     98
#define EXITPRIORITY           99

//告诉编译器在其他地方有一个名为main的函数，extern关键字表示这个函数的定义在其他地方
//而不是在当前文件中
extern int main(int argc, char** argv);

namespace SST {

/** Base class for all Activities in the SST Event Queue */
//定义了一个activity的类，它是SST命名空间下的一个基类，用于表示SST事件队列中的所有活动，此外他还定义
//了一个用于排序的内部类less，这个类模板可以根据不同的标准来比较两个Activity对象
//他继承自MemPoolItem，意味着它是用于内存池管理的
class Activity : public SST::Core::MemPoolItem
{
public:
    Activity() : delivery_time(0), priority_order(0), queue_order(0) {}
    virtual ~Activity() {}

    /**
       Class to use as the less than operator for STL functions or
       sorting algorithms.  If a template parameter is set to true,
       then that variable will be included in the comparison.  The
       parameters are: T - delivery time, P - priority and order tag,
       Q - queue order.
     */
    //定义了一个模板类less的参数化版本，这种代码可以根据不同类型或值的参数进行实例化
    template <bool T, bool P, bool Q>
    class less
    {
    public:
        //定义了一个操作符operator(),它用于比较两个Activity对象。这个操作符可以根据T、P、Q
        //三个模板参数的布尔值来决定比较的依据
        inline bool operator()(const Activity* lhs, const Activity* rhs) const
        {
            //T如果为true，则根据delivery_time(交付时间)进行比较
            if ( T && lhs->delivery_time != rhs->delivery_time ) return lhs->delivery_time < rhs->delivery_time;
            //P如果为true，则在delivery_time相同的情况下，根据priority_order(优先级顺序)进行比较
            if ( P && lhs->priority_order != rhs->priority_order ) return lhs->priority_order < rhs->priority_order;
            //Q如果为true，则在d、p都相同的情况下，根据queue_order(队列顺序)进行比较
            return Q && lhs->queue_order < rhs->queue_order;
        }

        // // Version without branching.  Still need to test to see if
        // // this is faster than the above implementation for "real"
        // // simulations.  For the sst-benchmark, the above is slightly
        // // faster.  Uses the bitwise operator because there are no
        // // early outs.  For bools, this is logically equivalent to the
        // // logical operators
        // inline bool operator()(const Activity* lhs, const Activity* rhs) const
        // {
        //     return ( T & lhs->delivery_time < rhs->delivery_time ) |
        //         (P & lhs->delivery_time == rhs->delivery_time & lhs->priority_order < rhs->priority_order) |
        //         (Q & lhs->delivery_time == rhs->delivery_time & lhs->priority_order == rhs->priority_order &
        //         lhs->queue_order < rhs->queue_order);
        // }

        // // Version without ifs, but with early outs in the logic.
        // inline bool operator()(const Activity* lhs, const Activity* rhs) const
        // {
        //     return ( T && lhs->delivery_time < rhs->delivery_time ) |
        //         (P && lhs->delivery_time == rhs->delivery_time && lhs->priority_order < rhs->priority_order) |
        //         (Q && lhs->delivery_time == rhs->delivery_time && lhs->priority_order == rhs->priority_order &&
        //         lhs->queue_order < rhs->queue_order);
        // }
    };

    /**
       Class to use as the greater than operator for STL functions or
       sorting algorithms (used if you want to sort opposite the
       natural soring order).  If a template parameter is set to true,
       then that variable will be included in the comparison.  The
       parameters are: T - delivery time, P - priority and order tag,
       Q - queue order.
     */
    
    template <bool T, bool P, bool Q>
    //这个和之前的less类似，但是它用于按照相反的顺序进行比较，即按照降序排列
    class greater
    {
    public:
        inline bool operator()(const Activity* lhs, const Activity* rhs) const
        {
            if ( T && lhs->delivery_time != rhs->delivery_time ) return lhs->delivery_time > rhs->delivery_time;
            if ( P && lhs->priority_order != rhs->priority_order ) return lhs->priority_order > rhs->priority_order;
            return Q && lhs->queue_order > rhs->queue_order;
        }

        // // Version without branching.  Still need to test to see if
        // // this is faster than the above implementation for "real"
        // // simulations.  For the sst-benchmark, the above is slightly
        // // faster.  Uses the bitwise operator because there are no
        // // early outs.  For bools, this is logically equivalent to the
        // // logical operators
        // inline bool operator()(const Activity* lhs, const Activity* rhs) const
        // {
        //     return ( T & lhs->delivery_time > rhs->delivery_time ) |
        //         (P & lhs->delivery_time == rhs->delivery_time & lhs->priority_order > rhs->priority_order) |
        //         (Q & lhs->delivery_time == rhs->delivery_time & lhs->priority_order == rhs->priority_order &
        //         lhs->queue_order > rhs->queue_order);
        // }

        // // Version without ifs, but with early outs in the logic.
        // inline bool operator()(const Activity* lhs, const Activity* rhs) const
        // {
        //     return ( T && lhs->delivery_time > rhs->delivery_time ) |
        //         (P && lhs->delivery_time == rhs->delivery_time && lhs->priority_order > rhs->priority_order) |
        //         (Q && lhs->delivery_time == rhs->delivery_time && lhs->priority_order == rhs->priority_order &&
        //         lhs->queue_order > rhs->queue_order);
        // }
    };

   
    /** Function which will be called when the time for this Activity comes to pass. */
    virtual void execute(void) = 0;

    /** Set the time for which this Activity should be delivered */
    //设置活动的交付时间
    inline void setDeliveryTime(SimTime_t time) { delivery_time = time; }

    /** Return the time at which this Activity will be delivered */
    //用于获取当前设置的交付时间
    inline SimTime_t getDeliveryTime() const { return delivery_time; }

    /** Return the Priority of this Activity */
    //获取活动的优先级，通过priority_order的高位部分获取，通过将其右移32位
    inline int getPriority() const { return (int)(priority_order >> 32); }

    /** Sets the order tag */
    //用于设置与活动关联的顺序标签，用于在具有相同优先级的活动中进一步确定执行顺序
    inline void setOrderTag(uint32_t tag) { priority_order = (priority_order & 0xFFFFFFFF00000000ul) | (uint64_t)tag; }

    /** Return the order tag associated with this activity */
    //获取当前设置的顺序标签
    inline uint32_t getOrderTag() const { return (uint32_t)(priority_order & 0xFFFFFFFFul); }

    /** Returns the queue order associated with this activity */
    //用于获取与活动观念连的队列顺序，这个顺序用于在事件队列中确定活动的处理顺序
    inline uint64_t getQueueOrder() const { return queue_order; }

    /** Get a string represenation of the event.  The default version
     * will just use the name of the class, retrieved through the
     * cls_name() function inherited from the serialzable class, which
     * will return the name of the last class to call one of the
     * serialization macros (ImplementSerializable(),
     * ImplementVirtualSerializable(), or NotSerializable()).
     * Subclasses can override this function if they want to add
     * additional information.
     */
    std::string toString() const override
    {
        std::stringstream buf;

        buf << cls_name() << " to be delivered at " << getDeliveryTimeInfo();
        return buf.str();
    }
//只有当预处理宏__SST_DEBUG_EVENT_TRACKING__ 被定义时，编译器才会编译 printTrackingInfo 函数。
#ifdef __SST_DEBUG_EVENT_TRACKING__
    //用于打印活动的跟踪信息，函数参数header和out被标记为UNSED，表示它们在函数体内不会被使用
    virtual void printTrackingInfo(const std::string& UNUSED(header), Output& UNUSED(out)) const {}
#endif

protected:
    /** Set the priority of the Activity */
    //设置活动的优先级，他将priority_order成员变量的低32位保留，并将传入的priority
    //值左移32位后设置到priority_order的高32位
    void setPriority(uint64_t priority) { priority_order = (priority_order & 0x00000000FFFFFFFFul) | (priority << 32); }

    /**
       Gets the delivery time info as a string.  To be used in
       inherited classes if they'd like to overwrite the default print
       or toString()
     */
    std::string getDeliveryTimeInfo() const
    {   
        //用于获取活动的交付时间信息，并将其格式化为字符串，这个字符串包括交付时间、优先级
        //顺序标签和队列顺序
        std::stringstream buf;
        buf << "time: " << delivery_time << ", priority: " << getPriority() << ", order tag: " << getOrderTag()
            << ", queue order: " << getQueueOrder();
        return buf.str();
    }

    // Function used by derived classes to serialize data members.
    // This class is not serializable, because not all class that
    // inherit from it need to be serializable.
    //用于序列化活动的成员变量，它重写了基类中的serialize_order函数，序列化是将对象转化为
    //可以存储或传输的形式的过程
    void serialize_order(SST::Core::Serialization::serializer& ser) override
    {
        ser& delivery_time;
        ser& priority_order;
        ser& queue_order;
    }
    ImplementVirtualSerializable(SST::Activity)


        /** Set a new Queue order */
        void setQueueOrder(uint64_t order)
    {
        queue_order = order;
    }

private:
    // Data members
    SimTime_t delivery_time;
    // This will hold both the priority (high bits) and the link order
    // (low_bits)
    uint64_t  priority_order;
    // Used for TimeVortex implementations that don't naturally keep
    // the insertion order
    uint64_t  queue_order;
};

} // namespace SST

#endif // SST_CORE_ACTIVITY_H
