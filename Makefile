# Copyright (c) 2018-2022 Jyoti Parwatikar 
# and Washington University in St. Louis
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
#    limitations under the License.
#

NDN-CXX = /usr/local
include ./Makefile.inc

DEBUG = -g

#CXXFLAGS = ${DEBUG} -std=c++14 -DBOOST_LOG_DYN_LINK -fpermissive #-DPTHREADS -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE -W -Wall -Wundef -Wimplicit -Wno-deprecated -Woverloaded-virtual 
CXXFLAGS = ${DEBUG} -std=c++14 -DBOOST_LOG_DYN_LINK -fpermissive -fPIC #-D_GLIBCXX_USE_CXX11_ABI=0 #-DPTHREADS -D_GNU_SOURCE -D_REENTRANT -D_THREAD_SAFE -W -Wall -Wundef -Wimplicit -Wno-deprecated -Woverloaded-virtual 
INCLUDES = -I. -I${NDN-CXX}/include #/ndn-cxx

#LIBS =  -lsqlite3 -lrt -lboost_system -lboost_filesystem -lboost_log -lcrypto++ -lpthread -lndn-cxx
LIBS =  -lrt -lboost_system -lboost_filesystem -lboost_log -lcrypto++ -lpthread -L${NDN-CXX}/lib -lndn-cxx -lprotobuf

LOCAL_LIB = $(OBJDIR)/libictsync_cxx.a
LOCAL_SHARED_LIB = $(OBJDIR)/libictsync_cxx.so
OBJS = $(OBJDIR)/ictsync.o \
       $(OBJDIR)/ict-vector-state.o \
       $(OBJDIR)/pending-interests.o

PROTO_OBJS = $(OBJDIR)/sync-state.pb.o 


all: ${OBJDIR} ${OBJS} ${PROTO_OBJS} ${LOCAL_LIB} ${LOCAL_SHARED_LIB}

install: clean all

${OBJDIR} :
	mkdir ${OBJDIR}

$(OBJS) : ${OBJDIR}/%.o : %.cpp
	${CXX} ${CXXFLAGS} ${INCLUDES} -o $@ -c $<
$(PROTO_OBJS) : ${OBJDIR}/%.o : %.cc
	${CXX} ${CXXFLAGS} ${INCLUDES} -o $@ -c $<

$(NOTIFY_OBJS) : ${OBJDIR}/%.o : %.cpp
	${CXX} ${CXXFLAGS} -D NOTIFY -D NDN_CXX_V05 ${NINCLUDES} -o $@ -c $<

$(LOCAL_LIB) : ${OBJDIR}/% : ${OBJS} ${PROTO_OBJS}
	$(AR) rc $@ ${OBJS} ${PROTO_OBJS}
	$(RANLIB) $@

$(LOCAL_SHARED_LIB) : ${OBJDIR}/% : ${OBJS} ${PROTO_OBJS}
	$(CXX) -shared -std=c++14 -o $@ ${OBJS} ${PROTO_OBJS}


clean:
	rm -f ${OBJDIR}/*
