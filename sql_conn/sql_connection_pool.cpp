#include<mysql/mysql.h>
#include<stdio.h>
#include<string>
#include<string.h>
#include<list>
#include<stdlib.h>
#include<pthread.h>
#include<iostream>
#include"sql_connection_pool.h"

using namespace std;

Connection_pool::Connection_pool(){
    this->CurConn = 0;
    this->FreeConn = 0;
}

Connection_pool *Connection_pool::GetInstance(){
    static Connection_pool connPool;
    return &connPool;
}

void Connection_pool::init(string url, string user, string password, string dbname, int port, unsigned int maxconn, int close_log){
    this->url = url;
    this->port = port;
    this->user = user;
    this->password = password;
    this->databasename = dbname;
    m_close_log = close_log;

    lock.lock();
    // printf("mysql initialization start...\n");
    for(int i = 0; i < maxconn; i++){
        MYSQL *conn = nullptr;
        conn = mysql_init(conn);
        if(conn == NULL){
            cout << "error_mysql_init: " << mysql_error(conn);
            exit(1);
        }
        conn = mysql_real_connect(conn, url.c_str(), user.c_str(), password.c_str(), dbname.c_str(), port, NULL, 0);
        if(conn == NULL){
            cout << "error_mysql_real_connect: " << mysql_error(conn);
            
            exit(1);
        }
        // printf("mysql intialization finished...\n");
        connList.push_back(conn);
        ++FreeConn;
    }

    reserve = sem(FreeConn);
    this->MaxConn = FreeConn;
    lock.unlock();
}

MYSQL *Connection_pool::GetConnection(){
    MYSQL *conn = nullptr;
    if(connList.size() == 0){
        return nullptr;
    }

    reserve.wait();

    lock.lock();

    conn = connList.front();
    connList.pop_front();

    --FreeConn;
    ++CurConn;

    lock.unlock();
    return conn;
}

bool Connection_pool::ReleaseConnection(MYSQL *conn){
    if(conn == nullptr){
        return false;
    }

    lock.lock();

    connList.push_back(conn);
    ++FreeConn;
    --CurConn;

    lock.unlock();
    reserve.post();
    return true;
}

void Connection_pool::DestroyPool(){
    lock.lock();
    if(connList.size() > 0){
        list<MYSQL *>::iterator it;
        for(it = connList.begin(); it != connList.end(); it++){
            MYSQL *conn = *it;
            mysql_close(conn);
        }

        CurConn = 0;
        FreeConn = 0;
        connList.clear();
        lock.unlock();
    }
    lock.unlock();
}

int Connection_pool::GetFreeConn(){
    return this->FreeConn;
}

Connection_pool::~Connection_pool(){
    DestroyPool();
}

ConnectionRAII::ConnectionRAII(MYSQL **sql, Connection_pool *connPool){
    *sql = connPool->GetConnection();

    connRAII = *sql;
    poolRAII = connPool;
}

ConnectionRAII::~ConnectionRAII(){
    poolRAII->ReleaseConnection(connRAII);
}