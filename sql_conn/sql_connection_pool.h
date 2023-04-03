#ifndef CONNECTION_POLL_
#define CONNECTION_POLL_

#include<stdio.h>
#include<list>
#include<mysql/mysql.h>
#include<error.h>
#include<string.h>
#include<iostream>
#include<string>
#include"../locker/locker.h"

using namespace std;

class Connection_pool{
public:
    MYSQL *GetConnection();
    bool ReleaseConnection(MYSQL *conn);
    int GetFreeConn();
    void DestroyPool();

    static Connection_pool *GetInstance();

    void init(string url, string user, string password, string dbname, int port, unsigned int maxconn, int close_log);

    Connection_pool();
    ~Connection_pool();
private:
    unsigned int MaxConn;
    unsigned int CurConn;
    unsigned int FreeConn;
    locker lock;
    sem reserve;

    list<MYSQL *> connList;

    string url;
    string port;
    string user;
    string password;
    string databasename;
    int m_close_log;
};

class ConnectionRAII{
public:
    ConnectionRAII(MYSQL **conn, Connection_pool *connpool);
    ~ConnectionRAII();
private:
    MYSQL *connRAII;
    Connection_pool *poolRAII;
};

#endif