#include "base_station.h"
#include "cloud_server.h"
#include "format_helper.hpp"
#include "message.h"
#include "ns3/ipv4.h"
#include "ns3/csma-module.h"
#include "ns3/internet-module.h"
#include <algorithm>  // for std::ranges::for_each
#define FMT_HEADER_ONLY
#include <fmt/core.h>


#define CHECK_INDEX(index) \
if (index > size()) throw std::out_of_range{"index out of range"}


namespace okec
{

base_station::base_station()
    : m_edge_devices{ nullptr },
      m_udp_application{ ns3::CreateObject<udp_application>() },
      m_node{ ns3::CreateObject<Node>() }
{
    m_udp_application->SetStartTime(Seconds(0));
    m_udp_application->SetStopTime(Seconds(10));

    // 为当前设备安装通信功能
    m_node->AddApplication(m_udp_application);

    // 设置处理回调函数
    m_udp_application->set_request_handler(message_dispatching, [this](Ptr<Packet> packet, const Address& remote_address) {
        this->on_dispatching_message(packet, remote_address);
    });

    // m_udp_application->set_request_handler(message_offloading_task, [this](Ptr<Packet> packet, const Address& remote_address) {
    //     this->on_offloading_message(packet, remote_address);
    // });
}

auto base_station::connect_device(edge_device_container& devices) -> void
{
    m_edge_devices = &devices;
}

auto base_station::has_free_resource(const task& t) const -> bool
{
    for (auto device : *m_edge_devices) {
        if (device->free_cpu_cycles() > t.needed_cpu_cycles() &&
            device->free_memory() > t.needed_memory() &&
            device->price() <= t.budget())
            return true;
    }

    return false;
}

auto base_station::get_address() const -> ns3::Ipv4Address
{
    auto ipv4 = m_node->GetObject<ns3::Ipv4>();
    return ipv4->GetAddress(1, 0).GetLocal();
}

auto base_station::get_port() const -> uint16_t
{
    return m_udp_application->get_port();
}

auto base_station::get_nodes(ns3::NodeContainer &nodes) -> void
{
    nodes.Add(m_node); // BS
    m_edge_devices->get_nodes(nodes); // EdgeDevice
}

auto base_station::get_node() -> Ptr<Node>
{
    return m_node;
}

auto base_station::link_cloud(const okec::cloud_server& cs) -> void
{
    if (!cs.get_address().IsInitialized())
    {
        fmt::print("link_cloud() Error: the network of cloud server is not initialized at this "
                    "time.\n");
        exit(0);
    }

    m_cs_address = std::make_pair(cs.get_address(), cs.get_port());
}

auto base_station::push_base_stations(base_station_container* base_stations) -> void
{
    m_base_stations = base_stations;
}

auto base_station::set_request_handler(std::string_view msg_type, callback_type callback) -> void
{
    m_udp_application->set_request_handler(msg_type, 
        [callback, this](Ptr<Packet> packet, const Address& remote_address) {
            callback(this, packet, remote_address);
        });
}

auto base_station::get_edge_devices() const -> edge_device_container
{
    return *m_edge_devices;
}

auto base_station::write(Ptr<Packet> packet, Ipv4Address destination, uint16_t port) const -> void
{
    m_udp_application->write(packet, destination, port);
}

auto base_station::erase_dispatching_record(const std::string& task_id) const -> void
{
    m_base_stations->erase_dispatching_record(task_id);
}

auto base_station::dispatched(const std::string& task_id, const std::string& bs_ip) const -> bool
{
    return m_base_stations->dispatched(task_id, bs_ip);
}

auto base_station::dispatching_record(const std::string& task_id) const -> void
{
    // 标记当前基站已经处理过该任务，但没有处理成功
    m_base_stations->dispatching_record(task_id, fmt::format("{:ip}", this->get_address()));
}

auto base_station::detach(detach_predicate_type pred, detach_result_type yes, detach_result_type no) const -> void
{
    if (auto it = std::find_if(m_base_stations->begin(), m_base_stations->end(), 
        pred); it != m_base_stations->end()) {
        // 分发到其他基站处理
        yes((*it)->get_address(), (*it)->get_port());
    } else {
        // 分发到云服务器处理
        no(m_cs_address.first, m_cs_address.second);  
    }
}

auto base_station::task_sequence(Ptr<task> t) -> void
{
    m_task_sequence.push_back(t);
}

auto base_station::on_dispatching_message(Ptr<Packet> packet, const Address& remote_address) -> void
{
    ns3::InetSocketAddress inetRemoteAddress = ns3::InetSocketAddress::ConvertFrom(remote_address);
    fmt::print("bs[{:ip}] receives the request from {:ip},", 
        get_address(), inetRemoteAddress.GetIpv4(), m_edge_devices->get_device(0)->get_address());

    bool handled{};
    auto msg = message::from_packet(packet);
    auto t = msg.to_task();

    for (auto device : *m_edge_devices) {
        if (device->free_cpu_cycles() > t->needed_cpu_cycles() &&
            device->free_memory() > t->needed_memory() &&
            device->price() <= t->budget()) {

            fmt::print(" dispatching it to {:ip} to handle the concrete tasks.\n", device->get_address());

            // 能够处理
            msg.type(message_handling);
            m_udp_application->write(msg.to_packet(), device->get_address(), device->get_port());
            handled = true;

            // 反服务器返回分发成功消息，以便服务器清除某些记录
            msg.type(message_dispatching_success);
            m_udp_application->write(msg.to_packet(), m_cs_address.first, m_cs_address.second);

            break;
        }
    }

    // 不能处理，消息需要再次转发
    if (!handled) {
        fmt::print(" dispatching it to {:ip} bacause of lacking resource.\n", m_cs_address.first);
        msg.type(message_dispatching_failure);
        m_udp_application->write(msg.to_packet(), m_cs_address.first, m_cs_address.second);
    }
}

auto base_station::on_offloading_message(Ptr<Packet> packet, const Address& remote_address) -> void
{
    ns3::InetSocketAddress inetRemoteAddress = ns3::InetSocketAddress::ConvertFrom(remote_address);
    fmt::print("bs[{:ip}] receives the offloading request from {:ip},", 
        get_address(), inetRemoteAddress.GetIpv4());

    bool handled{};
    auto msg = message::from_packet(packet);
    auto t = msg.to_task();

    for (auto device : *m_edge_devices) {
        if (device->free_cpu_cycles() > t->needed_cpu_cycles() &&
            device->free_memory() > t->needed_memory() &&
            device->price() <= t->budget()) {

            fmt::print(" dispatching it to {:ip} to handle the concrete tasks.\n", device->get_address());

            // 能够处理
            msg.type(message_handling);
            m_udp_application->write(msg.to_packet(), device->get_address(), device->get_port());
            handled = true;
            
            // 擦除分发记录
            m_base_stations->erase_dispatching_record(t->id());

            break;
        }
    }

    // 不能处理，消息需要再次转发
    if (!handled) {

        // 
        // auto current_bs = [this](std::shared_ptr<base_station> bs) {
        //     auto this_addr = fmt::format("{:ip}", this->get_address());
        //     auto current_addr = fmt::format("{:ip}", bs->get_address());
        //     // fmt::print("this addr: {}, current addr: {}\n", this_addr, current_addr);
        //     return this_addr == current_addr;
        // };

        // 是否还未分发到过此基站
        auto non_dispatched_bs = [&t, this](std::shared_ptr<base_station> bs) {
            return m_base_stations->dispatched(t->id(), fmt::format("{:ip}", bs->get_address()));
        };

        // 标记当前基站已经处理过该任务，但没有处理成功
        m_base_stations->dispatching_record(t->id(), fmt::format("{:ip}", this->get_address()));
        // if (auto it = std::find_if(m_base_stations->begin(), m_base_stations->end(), 
        //     current_bs); it != m_base_stations->end()) {
        //     // fmt::print("current bs: {}", (*it)->get_address());
        //     // 记录处理当前基站已经处理过该任务
        //     m_base_stations->dispatching_record(t->id(), fmt::format("{:ip}", (*it)->get_address()));
        // }

        // 搜索看其他基站是否还没有处理过该任务
        if (auto it = std::find_if(m_base_stations->begin(), m_base_stations->end(), 
            non_dispatched_bs); it != m_base_stations->end()) {
            fmt::print(" dispatching it to bs[{:ip}] bacause of lacking resource.\n", (*it)->get_address());
            
            // 分发到其他基站处理
            m_udp_application->write(packet, (*it)->get_address(), (*it)->get_port());
        } else {
            fmt::print(" dispatching it to {:ip} bacause of lacking resource.\n", m_cs_address.first);

            // 分发到云服务器处理
            msg.type(message_handling);
            m_udp_application->write(msg.to_packet(), m_cs_address.first, m_cs_address.second);

            // 擦除分发记录
            m_base_stations->erase_dispatching_record(t->id());
        }
    }
}

base_station_container::base_station_container(std::size_t n)
{
    m_base_stations.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        auto bs = std::make_shared<base_station>();
        bs->push_base_stations(this);
        m_base_stations.emplace_back(bs);
    }
}

auto base_station_container::link_cloud(const cloud_server& cs) -> void
{
    std::ranges::for_each(m_base_stations, [&cs](pointer_t bs) {
        bs->link_cloud(cs);
    });
}

auto base_station_container::operator[](std::size_t index) -> pointer_t
{
    return this->get(index);
}

auto base_station_container::operator()(std::size_t index) -> pointer_t
{
    return this->get(index);
}

auto base_station_container::get(std::size_t index) -> pointer_t
{
    CHECK_INDEX(index);
    return m_base_stations[index];
}

auto base_station_container::size() -> std::size_t
{
    return m_base_stations.size();
}

auto base_station_container::dispatching_record(const std::string& task_id, const std::string& bs_ip) -> void
{
    m_dispatching_record.emplace(task_id, bs_ip);
}

auto base_station_container::dispatched(const std::string& task_id, const std::string& bs_ip) -> bool
{
    auto records = m_dispatching_record.equal_range(task_id);
    for (auto i = records.first; i != records.second; ++i) {
        if (i->second == bs_ip)
            return false;
    }

    return true;
}

auto base_station_container::erase_dispatching_record(const std::string& task_id) -> void
{
    m_dispatching_record.erase(task_id);
}

auto base_station_container::set_request_handler(
    std::string_view msg_type, callback_type callback) -> void
{
    std::ranges::for_each(m_base_stations,
        [&msg_type, callback](pointer_t bs) {
        bs->set_request_handler(msg_type, callback);
    });
}


} // namespace okec