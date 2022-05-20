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

namespace str_tool
{
    // ("",  '.') -> [""]
    // ("11", '.') -> ["11"]
    // ("..", '.') -> ["", "", ""]
    // ("11.", '.') -> ["11", ""]
    // (".11", '.') -> ["", "11"]
    // ("11.22", '.') -> ["11", "22"]
    vector<std::string> split(const string& str, char d)
    {
        vector<std::string> r;

        string::size_type start = 0;
        string::size_type stop = str.find_first_of(d);

        string substr;

        while (stop != string::npos)
        {
            substr = str.substr(start, stop - start);
            if(substr != "")
                r.push_back(substr);

            start = stop + 1;
            stop = str.find_first_of(d, start);
        }

        substr = str.substr(start);
        if(substr != "")
            r.push_back(substr);

        return r;
    }

    size_t replace_all(string& inout, string_view what, string_view with)
    {
        size_t count{};
        for (string::size_type pos{};
            inout.npos != (pos = inout.find(what.data(), pos, what.length()));
            pos += with.length(), ++count)
        {
            inout.replace(pos, what.length(), with.data(), with.length());
        }
        return count;
    }
}

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

    uint16_t m_port;
    size_t m_bulk_size;

    async::handle_t m_static_cmd;

    asio::io_service m_service;
    boost::thread m_server_th;
    
    logger log;

    bool m_started = false;

    void client_session(bulk_client_ptr client)
    {
        char recv_data[512];
        string buffer = "";
        size_t dynamic_counter = 0;
        bool dynamic = false;

        async::handle_t dynamic_cmd = async::connect(m_bulk_size);

        while (true)
        {
            size_t len = 0;

            boost::system::error_code ec;
            len = client->m_sock->receive(asio::buffer(recv_data), 0, ec);

            if (len)
                buffer.append(recv_data, len);

            if (buffer.find('\n') != string::npos)
            {
                vector<string> cmd_vec = str_tool::split(buffer, '\n');

                for (string cmd : cmd_vec)
                {
                    //Remove all escape characters from buffer
                    str_tool::replace_all(cmd, "\r", "");
                    str_tool::replace_all(cmd, "\n", "");

                    log << cmd << "\r\n";

                    if (cmd == "{")
                    {
                        dynamic_counter++;
                        dynamic = true;
                    }

                    if (cmd == "}")
                    {
                        if (dynamic_counter > 0)
                            dynamic_counter--;
                    }

                    //static cmd going to shared m_static_cmd processor
                    //dynamic cmd going to client specific dynamic_cmd processor
                    if (dynamic)
                        async::receive(dynamic_cmd, cmd.c_str(), cmd.length());
                    else
                        async::receive(m_static_cmd, cmd.c_str(), cmd.length());

                    if (dynamic_counter == 0)
                        dynamic = false;
                }

                buffer.clear();
            }

            if (ec.value())
            {
                //Client fell off
                {
                    lock_guard<mutex> lock(m_list_lock);
                    m_client_list.remove(client);
                }

                if (dynamic_cmd)
                    async::disconnect(dynamic_cmd);

                log << "Client fell off\r\n";
                return;
            }
        }
    }


    void accept_handler(const boost::system::error_code& error,
                        socket_ptr sock,
                        asio::ip::tcp::acceptor& acceptor)
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
                                asio::placeholders::error,
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
    bulk_server(uint16_t port, size_t bulk_size) : m_port(port), m_bulk_size(bulk_size)
    {
        m_static_cmd = async::connect(bulk_size);
    }

    ~bulk_server()
    {
        if (m_started)
            stop();

        if (m_static_cmd)
            async::disconnect(m_static_cmd);
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
        m_started = true;

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
            log << "Shutdown client\r\n";
            client->m_sock->cancel();
            client->m_sock->close();
            boost::system::error_code ec = asio::error::connection_aborted;
            client->m_sock->shutdown(socket::shutdown_both, ec);
            client->m_sock->release(ec);
            client->m_serve_th.join();
        }

        m_started = false;
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
            ("port", po::value<uint16_t>()->default_value(9000), "port")
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

    uint16_t port = vm["port"].as<uint16_t>();
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