# Copyright (c) 2018-2022 Charlie Wiseman, Jyoti Parwatikar, John DeHart 
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


MyOS   ?= $(shell uname -s)
MyVER  ?= $(shell uname -r | awk -F. '{print $$1$$2}')
MyMACH ?= $(shell uname -m)

ifeq ($(MyMACH), x86_64)
  MyARCH ?= 64
else
  MyARCH ?= 32
endif

OBJDIR ?= $(MyOS)$(MyVER).$(MyARCH)

PREFIX ?= 

AR     = $(PREFIX)ar
AS     = $(PREFIX)as
CC     = $(PREFIX)gcc
CXX    = $(PREFIX)g++
RANLIB = $(PREFIX)ranlib
LD     = $(PREFIX)ld
NM     = $(PREFIX)nm
