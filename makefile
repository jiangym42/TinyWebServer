CXX ?= g++

server: ./main.cpp ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp ./http_conn/http_conn.cpp ./http_conn/http_conn.h ./log/log.h ./log/log.cpp ./log/block_queue.h ./sql_conn/sql_connection_pool.h ./sql_conn/sql_connection_pool.cpp  ./webserver/webserver.h ./webserver/webserver.cpp ./config/config.h ./config/config.cpp
	$(CXX) -o server ./main.cpp ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp ./http_conn/http_conn.cpp ./http_conn/http_conn.h ./log/log.h ./log/log.cpp ./log/block_queue.h ./sql_conn/sql_connection_pool.h ./sql_conn/sql_connection_pool.cpp  ./webserver/webserver.h ./webserver/webserver.cpp ./config/config.h ./config/config.cpp -lpthread -lmysqlclient

clean:
	rm -r server