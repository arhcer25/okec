///////////////////////////////////////////////////////////////////////////////
//   __  __ _  ____  ___ 
//  /  \(  / )(  __)/ __) OKEC(a.k.a. EdgeSim++)
// (  O ))  (  ) _)( (__  version 1.0.1
//  \__/(__\_)(____)\___) https://github.com/dxnu/okec
// 
// Copyright (C) 2023-2024 Gaoxing Li
// Licenced under Apache-2.0 license. See LICENSE.txt for details.
///////////////////////////////////////////////////////////////////////////////

#ifndef OKEC_EDGE_DEVICE_H_
// 头文件保护宏的开始
#define OKEC_EDGE_DEVICE_H_
// 定义头文件保护宏

#include <okec/common/resource.h>
// 包含资源管理相关的头文件
#include <okec/network/udp_application.h>
// 包含UDP网络应用相关的头文件


namespace okec
// 定义okec命名空间
{

class simulator;
// 前向声明simulator类
class task;
// 前向声明task类


class edge_device
// 定义边缘设备类
{
    using callback_type  = std::function<void(edge_device*, ns3::Ptr<ns3::Packet>, const ns3::Address&)>;
    // 定义回调函数类型,用于处理网络消息

public:
    edge_device(simulator& sim);
    // 构造函数,接收模拟器引用作为参数

    // 返回当前设备的IP地址
    auto get_address() const -> ns3::Ipv4Address;
    // 获取设备IP地址的成员函数

    // 返回当前设备的端口号
    auto get_port() const -> uint16_t;
    // 获取设备端口号的成员函数

    auto get_node() -> ns3::Ptr<ns3::Node>;
    // 获取NS3节点指针的成员函数

    // 获取当前设备绑定的资源
    auto get_resource() -> ns3::Ptr<resource>;
    // 获取设备资源的成员函数

    // 为当前设备安装资源
    auto install_resource(ns3::Ptr<resource> res) -> void;
    // 安装资源到设备的成员函数

    auto set_position(double x, double y, double z) -> void;
    // 设置设备位置的成员函数
    auto get_position() -> ns3::Vector;
    // 获取设备位置的成员函数

    auto set_request_handler(std::string_view msg_type, callback_type callback) -> void;
    // 设置请求处理器的成员函数

    auto write(ns3::Ptr<ns3::Packet> packet, ns3::Ipv4Address destination, uint16_t port) const -> void;
    // 发送数据包的成员函数

private:
    auto on_get_resource_information(ns3::Ptr<ns3::Packet> packet, const ns3::Address& remote_address) -> void;
    // 处理获取资源信息请求的私有成员函数

public:
    simulator& sim_;
    // 模拟器引用成员变量
    ns3::Ptr<ns3::Node> m_node;
    // NS3节点指针成员变量
    ns3::Ptr<okec::udp_application> m_udp_application;
    // UDP应用程序指针成员变量
};


class edge_device_container
// 定义边缘设备容器类
{
    using value_type   = edge_device;
    // 定义容器值类型为edge_device
    using pointer_type = std::shared_ptr<value_type>;
    // 定义指针类型为edge_device的智能指针

public:
    edge_device_container(simulator& sim, std::size_t n);
    // 构造函数,接收模拟器引用和设备数量

    // 获取所有Nodes
    auto get_nodes(ns3::NodeContainer &nodes) -> void;
    // 获取所有节点的成员函数

    auto get_device(std::size_t index) -> pointer_type;
    // 根据索引获取设备的成员函数

    auto begin() -> std::vector<pointer_type>::iterator {
        return m_devices.begin();
    }
    // 返回容器起始迭代器的成员函数

    auto end() -> std::vector<pointer_type>::iterator {
        return m_devices.end();
    }
    // 返回容器结束迭代器的成员函数

    auto size() -> std::size_t;
    // 获取容器大小的成员函数

    auto install_resources(resource_container& res, int offset = 0) -> void;
    // 为容器中的设备安装资源的成员函数

private:
    std::vector<pointer_type> m_devices;
    // 存储设备智能指针的向量成员变量
};


} // namespace okec
// 结束okec命名空间

#endif // OKEC_EDGE_DEVICE_H_
// 结束头文件保护宏