// task_7_bulk.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <sstream>
#include <thread>
#include <list>
#include <boost/program_options.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/bind/bind.hpp>
#include <boost/shared_ptr.hpp>
#include "async.h"

using namespace std;
namespace asio = boost::asio;
namespace po = boost::program_options;

class logger
{
    bool m_enabled = false;

public:
    logger() {};

    void enable() { m_enabled = true; };
    void disable() { m_enabled = false; };

    logger& operator<<(const auto& data)
    {
        if (m_enabled)
            cout << data;

        return *this;
    }
};

struct bulk_client
{
    using socket = asio::ip::tcp::socket;
    typedef boost::shared_ptr<socket> socket_ptr;

    socket_ptr m_sock;
    boost::thread m_serve_th;
};

class bulk_server
{
    using socket = asio::ip::tcp::socket;
    typedef boost::shared_ptr<socket> socket_ptr;
    typedef boost::shared_ptr<bulk_client> bulk_client_ptr;

    list<bulk_client_ptr> m_client_list;
    mutex m_list_lock;

    asio::ip::port_type m_port;
    size_t m_bulk_size;

    async::handle_t m_static_proc;

    asio::io_service m_service;
    boost::thread m_server_th;
    
    logger log;

    void client_session(bulk_client_ptr client)
    {
        char recv_data[512];
        string buffer = "";

        //create dynamic block??
        async::handle_t m_dynamic_proc = async::connect(m_bulk_size);

        while (true)
        {
            size_t len = 0;

            boost::system::error_code ec;
            len = client->m_sock->receive(asio::buffer(recv_data), 0, ec);

            if (len)
                buffer.append(recv_data, len);

            if (buffer.find('\n') != -1)
            {
                //cout << buffer;
                log << buffer;
                buffer.clear();
            }

            if (ec)
            {
                //Client fell off
                {
                    lock_guard<mutex> lock(m_list_lock);
                    m_client_list.remove(client);
                }

                if (m_dynamic_proc)
                    async::disconnect(m_dynamic_proc);

                log << "Client fell off\r\n";
                return;
            }
        }
    }


    void accept_handler(const boost::system::error_code& error,
                        socket_ptr sock,
                        boost::asio::ip::tcp::acceptor& acceptor)
    {
        if (error)
            return;

        log << "New client connected\r\n";

        bulk_client_ptr client(new bulk_client());

        {
            lock_guard<mutex> lock(m_list_lock);
            client->m_sock = sock;
            client->m_serve_th = boost::thread(&bulk_server::client_session, this, client);
            m_client_list.push_back(client);
        }

        start_accept(acceptor);
    }

    void start_accept(asio::ip::tcp::acceptor& acc)
    {
        socket_ptr sock(new socket(m_service));
        
        acc.async_accept(*sock, boost::bind(&bulk_server::accept_handler,this,
                                boost::asio::placeholders::error,
                                sock,
                                boost::ref(acc)));
    }

    void server_thread()
    {
        asio::ip::tcp::endpoint ep(asio::ip::tcp::v4(), m_port);
        asio::ip::tcp::acceptor acc(m_service, ep);

        start_accept(acc);
        m_service.run();
    }

public:
    bulk_server(asio::ip::port_type port, size_t bulk_size) : m_port(port), m_bulk_size(bulk_size)
    {
        m_static_proc = async::connect(bulk_size);
    }

    ~bulk_server()
    {
        if (m_static_proc)
            async::disconnect(m_static_proc);
    }

    void set_verbose_out(bool enable)
    {
        if (enable)
            log.enable();
        else
            log.disable();
    }

    bool start()
    {
        log << "Start server thread\r\n";
        m_server_th = boost::thread(&bulk_server::server_thread, this);
        return true;
    }

    void stop()
    {
        log << "Stop server thread\r\n";

        m_service.stop();
        m_server_th.join();
        
        log << "Close client connections\r\n";

        while(true)
        {
            bulk_client_ptr client;

            {
                lock_guard<mutex> lock(m_list_lock);
                if (m_client_list.empty())
                    break;

                client = m_client_list.front();
            }

            client->m_sock->close();
            client->m_serve_th.join();
        }
    }
};


int main(int argc, const char* argv[])
{
    po::variables_map vm;
    po::positional_options_description pos_desc;

    try
    {
        po::options_description desc{ "Options" };
        desc.add_options()
            ("help,h", "This message")
            ("verbose,v", "Verbose output")
            ("port", po::value<asio::ip::port_type>()->default_value(9000), "port")
            ("bulk_size", po::value<size_t>()->default_value(5), "bulk_size");


        pos_desc.add("port", 1);
        pos_desc.add("bulk_size", 2);

        po::store(parse_command_line(argc, argv, desc), vm);
        po::notify(vm);

        if (vm.count("help"))
        {
            cout << desc << "\r\n";;
            return 0;
        }
        if (!vm.count("port"))
        {
            cout << desc << "\r\n";;
            return 0;
        }
        if (!vm.count("bulk_size"))
        {
            cout << desc << "\r\n";;
            return 0;
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << std::endl;
    }

    asio::ip::port_type port = vm["port"].as<asio::ip::port_type>();
    size_t bulk_size = vm["bulk_size"].as<size_t>();

    bulk_server srv(port, bulk_size);

    if (vm.count("verbose"))
        srv.set_verbose_out(true);
    
    srv.start();

    string cmd = "";

    while (getline(cin, cmd))
    {
        if (cmd == "stop")
            break;
    }

    srv.stop();

    /*   
    std::size_t bulk = 5;
    auto h = async::connect(bulk);
    auto h2 = async::connect(bulk);
    async::receive(h, "1", 1);
    async::receive(h2, "1\n", 2);
    async::receive(h, "\n2\n3\n4\n5\n6\n{\na\n", 15);
    async::receive(h, "b\nc\nd\n}\n89\n", 11);
    async::disconnect(h);
    async::disconnect(h2);
    */
    return 0;
}