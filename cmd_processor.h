#pragma once
#include <vector>
#include <deque>
#include <chrono>
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <array>
#include <condition_variable>

using namespace std;
using namespace std::chrono;

class cmd
{
    system_clock::time_point m_create_time;
    string body;

public:
    cmd(string cmd)
    {
        m_create_time = system_clock::now();
        body = cmd;
    }

    string execute()
    {
        return body;
    }

    uint64_t get_create_time()
    {
        auto time_point_ms = time_point_cast<milliseconds>(m_create_time);
        uint64_t value_ms = duration_cast<milliseconds>(time_point_ms.time_since_epoch()).count();
        return value_ms;
    }
};

class cmd_block
{
    deque<cmd> m_queue;

public:
    string m_file_name = "";

    bool empty()    { return m_queue.size() == 0;   }
    size_t size()   { return m_queue.size();        }

    void push_back(cmd command)
    {
        if (m_queue.size() == 0)
        {
            int random_variable = rand();
            m_file_name = "bulk" + to_string(command.get_create_time()) + "_" + to_string(random_variable) + ".log";
        }

        m_queue.push_back(command);
    }

    cmd pop_front()
    {
        cmd c = m_queue.front();
        m_queue.pop_front();
        return c;
    }

    void clear()
    {
        m_queue.clear();
    }
};

class cmd_processor
{
    cmd_block* m_cur_cmd_block;
    mutex m_cur_cmd_block_lock;

    deque<cmd_block*> m_log_queue;
    mutex m_log_queue_lock;

    //tuple<file_name, cmd, is_last_cmd>
    deque< tuple<string, string, bool> > m_file_queue;
    mutex m_file_queue_lock;

    condition_variable m_log_queue_check;
    condition_variable m_file_queue_check;
    bool m_log_done = false;
    bool m_file_done = false;

    thread m_log_th;
    vector<thread> m_file_th_vec;
    size_t m_f_th_count = 0;

    size_t dynamic_mode = 0;
    size_t m_N = 0;
    bool m_async = false;

    void sync_bulk()
    {
        if (m_cur_cmd_block->empty())
            return;

        //Create bulk line for output
        string bulk_str = "bulk: ";

        bool begin = true;

        while (!m_cur_cmd_block->empty())
        {
            cmd next = m_cur_cmd_block->pop_front();

            if (!begin)
                bulk_str += ", ";
            else
                begin = false;

            bulk_str += next.execute();// +", ";
        }

        bulk_str += "\r\n";
        cout << bulk_str;

        //Create log file
        ofstream fout(m_cur_cmd_block->m_file_name);
        fout << bulk_str;
        fout.close();
    }

    void async_bulk()
    {
        {
            unique_lock<mutex> lock(m_log_queue_lock);
            m_log_queue.push_back(m_cur_cmd_block);
        }
        m_cur_cmd_block = new cmd_block();
        m_log_queue_check.notify_one();
    }

    void log_th_f()
    {
        while(true)
        {
            cmd_block* tmp_pcmd_block = nullptr;
            //cmd_block tmp_cmd_block;

            {
                unique_lock<mutex> lock(m_log_queue_lock);

                while (!m_log_queue.size())
                {
                    if (m_log_done)
                    {
                        //End file threads
                        m_file_done = true;
                        m_file_queue_check.notify_all();
                        return;
                    }

                    m_log_queue_check.wait(lock);
                }

                tmp_pcmd_block = m_log_queue.front();
                m_log_queue.pop_front();
            }

            if (tmp_pcmd_block->empty())
                continue;

            //log
            //Create bulk line for output
            string bulk_str = "bulk: ";

            //Create file here
            ofstream fout(tmp_pcmd_block->m_file_name, ofstream::app);
            fout << bulk_str;
            fout.close();

            bool begin = true;

            while (!tmp_pcmd_block->empty())
            {
                cmd next = tmp_pcmd_block->pop_front();

                if (!begin)
                    bulk_str += ", ";
                else
                    begin = false;

                //execute cmd from block
                string file_cmd_record;
                file_cmd_record = next.execute();
                bulk_str += file_cmd_record;

                {
                    unique_lock<mutex> lock(m_file_queue_lock);

                    bool is_last_cmd = false;
                    if (!tmp_pcmd_block->size())
                        is_last_cmd = true;

                    m_file_queue.push_back(make_tuple(tmp_pcmd_block->m_file_name, file_cmd_record, is_last_cmd));
                }

                m_file_queue_check.notify_all();
            }

            if(tmp_pcmd_block)
                delete tmp_pcmd_block;

            bulk_str += "\r\n";
            cout << bulk_str;
        }
    }

    void file_th_f()
    {
        while (true)
        {
            tuple<string, string, bool> tmp_record;
            {
                unique_lock<mutex> lock(m_file_queue_lock);

                while (!m_file_queue.size())
                {
                    if (m_file_done)
                        return;

                    m_file_queue_check.wait(lock);
                }

                tmp_record = m_file_queue.front();
                m_file_queue.pop_front();

                string file_name;
                string file_cmd_record;
                bool is_last_cmd;

                tie(file_name, file_cmd_record, is_last_cmd) = tmp_record;

                //Create log file
                ofstream fout(file_name, ofstream::app);
                if (is_last_cmd)
                    fout << file_cmd_record;
                else
                    fout << file_cmd_record << ", ";
                fout.close();
            }
        }
    }

    void bulk()
    {
        return m_async ? async_bulk() : sync_bulk();
    }

    bool process_spec_ops(string cmd_body)
    {
        bool bspec_ops = false;

        if (cmd_body == "{")
        {
            if (!dynamic_mode)
                bulk();

            dynamic_mode++;
            bspec_ops = true;
        }

        if (cmd_body == "}")
        {
            if (dynamic_mode)
            {
                dynamic_mode--;

                if (!dynamic_mode)
                    bulk();
            }

            bspec_ops = true;
        }

        return bspec_ops;
    }

public:
    cmd_processor(size_t N, size_t f_th_count = 2) : m_N(N), m_f_th_count(f_th_count)
    {
        m_cur_cmd_block = new cmd_block();

        if (f_th_count)
        {
            m_async = true;

            //use current time as seed for random generator
            srand(static_cast<int>(time(nullptr))); 

            m_log_th = thread(&cmd_processor::log_th_f, this);

            for (size_t i = 0; i < f_th_count; i++)
                m_file_th_vec.push_back(thread(&cmd_processor::file_th_f, this));
        }
    }
    
    ~cmd_processor()
    {
        m_log_done = true;

        bulk();
        m_log_th.join();

        for (auto& t : m_file_th_vec)
            t.join();

        delete m_cur_cmd_block;
    }

    void add_cmd(string cmd_body)
    {
        if (m_async)
            m_cur_cmd_block_lock.lock();

        //acquare lock
        if (!process_spec_ops(cmd_body))
        {
            cmd new_cmd = cmd(cmd_body);

            //This is not spec ops cmd
            m_cur_cmd_block->push_back(new_cmd);

            if (!dynamic_mode)
            {
                if (m_cur_cmd_block->size() == m_N)
                    bulk();
            }
        }

        if (m_async)
            m_cur_cmd_block_lock.unlock();
    }
};