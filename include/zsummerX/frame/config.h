/*
 * zsummerX License
 * -----------
 *
 * zsummerX is licensed under the terms of the MIT license reproduced below.
 * This means that zsummerX is free software and can be used for both academic
 * and commercial purposes at absolutely no cost.
 *
 *
 * ===============================================================================
 *
 * Copyright (C) 2010-2015 YaweiZhang <yawei.zhang@foxmail.com>.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ===============================================================================
 *
 * (end of COPYRIGHT)
 */


//! common header 

#ifndef ZSUMMER_CONFIG_H_
#define ZSUMMER_CONFIG_H_

#include <iostream>
#include <queue>
#include <iomanip>
#include <string.h>
#include <signal.h>
#include <unordered_map>
#include <log4z/log4z.h>
#include <proto4z/proto4z.h>

#ifdef WIN32
#include "../iocp/iocp_impl.h"
#include "../iocp/tcpsocket_impl.h"
#include "../iocp/udpsocket_impl.h"
#include "../iocp/tcpaccept_impl.h"
#elif defined(__APPLE__) || defined(__SELECT__)
#include "../select/select_impl.h"
#include "../select/udpsocket_impl.h"
#include "../select/tcpsocket_impl.h"
#include "../select/tcpaccept_impl.h"
#else
#include "../epoll/epoll_impl.h"
#include "../epoll/tcpsocket_impl.h"
#include "../epoll/udpsocket_impl.h"
#include "../epoll/tcpaccept_impl.h"
#endif

namespace zsummer
{
    namespace network
    {
        //! define network type
        typedef unsigned int SessionID;
        const SessionID InvalidSeesionID = -1;
        typedef unsigned int AccepterID;
        const AccepterID InvalidAccepterID = -1;

        //! define session id range.
        const unsigned int __MIDDLE_SEGMENT_VALUE = 300 * 1000 * 1000;
        inline bool isSessionID(unsigned int unknowID){ return unknowID < __MIDDLE_SEGMENT_VALUE ? true : false; }
        inline bool isConnectID(unsigned int unknowID){ return unknowID != InvalidSeesionID && !isSessionID(unknowID); }
        inline unsigned int nextSessionID(unsigned int curSessionID){ return (curSessionID + 1) % __MIDDLE_SEGMENT_VALUE; }
        inline unsigned int nextConnectID(unsigned int curSessionID){ return (curSessionID + 1 < __MIDDLE_SEGMENT_VALUE || curSessionID + 1 == InvalidSeesionID) ? __MIDDLE_SEGMENT_VALUE : curSessionID + 1; }

        //----------------------------------------


        enum ProtoType
        {
            PT_TCP,
            PT_HTTP,
        };

        enum BLOCK_CHECK_TYPE
        {
            BCT_SUCCESS = 0, //成功, 返回当前block大小
            BCT_SHORTAGE = 1, //不足, 返回当前block还需要的大小
            BCT_CORRUPTION = 2, //错误, 需要关闭socket
        };

        enum StatType
        {
            TOTAL_SEND_COUNT,
            TOTAL_SEND_SIZE,
            TOTAL_RECV_COUNT,
            TOTAL_RECV_SIZE,
        };


        struct SessionBlock 
        {
            unsigned int len = 0;
            unsigned int bound = 0;
            char begin[0];
        };


        using CreateBlock = std::function<SessionBlock * ()>;

        using FreeBlock = std::function<void(SessionBlock *)>;
        
        using CheckBlockResult = std::pair<BLOCK_CHECK_TYPE, unsigned int>;

        //检查当前缓冲块内是否能读出一个完整的block
        using CheckBlock = std::function<CheckBlockResult(const char * /*begin*/, unsigned int /*len*/, unsigned int /*boundLen*/)>;

        //!每读出一个block就调用这个方法dispatch出去
        using DispatchBlock = std::function<bool(TcpSessionPtr &  /*session*/, const char * /*blockBegin*/, int /*blockSize*/)>;

        //!连接建立, 关闭, 定时器
        using SessionEvent = std::function<bool(TcpSessionPtr &  /*session*/)>;

        using PairString = std::pair<std::string, std::string>;
        using MapString = std::map<std::string, std::string>;
        //!HTTP解包,hadHeader 区别chunked的后续小包, commonLine 指的是GET, POST RESPONSE. 
        using CheckHTTPBlock = std::function<CheckBlockResult(const char * /*begin*/, unsigned int /*len*/, unsigned int /*boundLen*/,
            bool /*hadHeader*/, bool & /*isChunked*/, PairString& /*commonLine*/, MapString & /*head*/, std::string & /*body*/)>;
        //!HTTP派发
        using DispatchHTTPMessage = std::function<
            bool(TcpSessionPtr &/*session*/, const PairString & /*commonLine*/, const MapString &/*head*/, const std::string & /*body*/)>;
        
        inline SessionBlock * DefaultCreateBlock()
        {
            unsigned int boundLen = 100 * 1024;
            SessionBlock * p = (SessionBlock*)malloc(sizeof(SessionBlock)+boundLen);
            p->boundLen = boundLen;
            p->curLen = 0;
            return p;
        }

        inline void DefaultFreeBlock(SessionBlock *p)
        {
            free(p);
        }

        inline void dispatchSessionMessage(TcpSessionPtr  & session, const char * blockBegin, int blockSize)
        {

        }

        inline bool  dispatchHTTPMessage(TcpSessionPtr session, const zsummer::proto4z::PairString & commonLine, const zsummer::proto4z::HTTPHeadMap &head, const std::string & body)
        {
            MessageDispatcher::getRef().dispatchSessionHTTPMessage(session, commonLine, head, body);
        }
        struct SessionTraits 
        {
#pragma region CUSTOM
            ProtoType       _protoType = PT_TCP;
            std::string     _rc4TcpEncryption = ""; //empty is not encryption
            bool            _openFlashPolicy = false;
            bool            _setNoDelay = true;
            unsigned int    _pulseInterval = 5000;

            CheckBlock _checkTcpIntegrity = zsummer::proto4z::checkBuffIntegrity;
            DispatchBlock _dispatchBlock = dispatchSessionMessage;
            CheckHTTPBlock _checkHTTPIntegrity = zsummer::proto4z::checkHTTPBuffIntegrity;
            DispatchHTTPMessage _dispatchHTTP = dispatchHTTPMessage;
            SessionEvent _eventSessionClose;
            SessionEvent _eventSessionBuild;
            CreateBlock _createBlock;
            FreeBlock _freeBlock;
#pragma endregion CUSTOM


#pragma region CONNECT
            //! valid traits when session is connect
            unsigned int _reconnectMaxCount = 0; // try reconnect max count
            unsigned int _reconnectInterval = 5000; //million seconds;
            bool         _reconnectCleanAllData = true;//clean all data when reconnect;
#pragma endregion CONNECT

        };

        struct AccepterExtend
        {
            AccepterID _aID = InvalidAccepterID;
            TcpAcceptPtr _accepter;
            std::string        _listenIP;
            unsigned short    _listenPort = 0;
            unsigned int    _maxSessions = 5000;
            std::vector<std::string> _whitelistIP;
            unsigned long long _totalAcceptCount = 0;
            unsigned long long _currentLinked = 0;
            SessionTraits _sessionTraits;
        };


        class Any
        {
        public:
            Any(unsigned long long n)
            {
                _integer = n;
            }
            Any(const std::string & str)
            {
                _isInteger = false;
                _string = str;
            }
            bool _isInteger = true;
            unsigned long long _integer;
            std::string _string;
        };
        












        inline zsummer::log4z::Log4zStream & operator << (zsummer::log4z::Log4zStream &os, const SessionTraits & traits)
        {
            os << "{ " << "_protoType=" << traits._protoType
                << ", _rc4TcpEncryption=" << traits._rc4TcpEncryption
                << ", _openFlashPolicy=" << traits._openFlashPolicy
                << ", _setNoDelay=" << traits._setNoDelay
                << ", _pulseInterval=" << traits._pulseInterval
                << ", _reconnectMaxCount=" << traits._reconnectMaxCount
                << ", _reconnectInterval=" << traits._reconnectInterval
                << ", _reconnectCleanAllData=" << traits._reconnectCleanAllData
                << "}";
            return os;
        }


        inline zsummer::log4z::Log4zStream & operator << (zsummer::log4z::Log4zStream &os, const AccepterExtend & extend)
        {
            os << "{"
                << "_aID=" << extend._aID
                << ", _listenIP=" << extend._listenIP
                << ", _listenPort=" << extend._listenPort
                << ", _maxSessions=" << extend._maxSessions
                << ",_totalAcceptCount=" << extend._totalAcceptCount
                << ", _currentLinked=" << extend._currentLinked
                << ",_whitelistIP={";

            for (auto str : extend._whitelistIP)
            {
                os << str << ",";
            }
            os << "}";
            os << ", SessionTraits=" << extend._sessionTraits;
            os << "}";
            return os;
        }








    }
}





#endif

