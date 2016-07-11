DEBUG ?= 1
CC = gcc
CCFLAGS = -g -std=gnu99 
CXX = g++
CXXFLAGS = -g -std=c++11
TEST_PACKAGE = minicached_unit_test
TEST_PACKAGE_NAME = $(PACKAGE)
TEST_PACKAGE_STRING = $(PACKAGE_NAME)1.0
TEST_PACKAGE_VERSION = 1.0
SHELL = /bin/bash
VERSION = 1.0
SRC_DIRS = ../source
TEST_SRC_DIRS = ./
EXTRAFLAGS = -I../include -lcrypto -lrt -ljson-c -isystem -Wall -Wextra -lpthread

OBJDIR = ../obj
TEST_OBJDIR= ./obj

vpath %.c $(SRC_DIRS)
vpath %.c $(TEST_SRC_DIRS)

srcs = $(filter-out main.c, $(notdir $(wildcard $(SRC_DIRS)/*.c)))
objs = $(srcs:%.c=$(OBJDIR)/%.o)

test_srcs = $(filter-out unit_test_main.cpp, $(notdir $(wildcard $(TEST_SRC_DIRS)/*.cpp)))
test_objs = $(test_srcs:%.cpp=$(TEST_OBJDIR)/%.o)

all : $(TEST_PACKAGE)
.PHONY : all

$(TEST_PACKAGE) : $(objs) $(test_objs)
	$(CXX) -c $(CXXFLAGS) $(EXTRAFLAGS) $(TEST_SRC_DIRS)/unit_test_main.cpp -o $(TEST_OBJDIR)/unit_test_main.o
	$(CXX) $(CXXFLAGS) $(EXTRAFLAGS) $(TEST_OBJDIR)/unit_test_main.o -lgtest $^ -o $@

$(objs) : $(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	$(CC) -MMD -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@ 

$(test_objs): $(TEST_OBJDIR)/%.o: %.cpp
	@mkdir -p $(TEST_OBJDIR)
	$(CXX) -MMD -c $(CXXFLAGS) $(EXTRAFLAGS) $< -o $@ 

#check header for obj reconstruction
-include $(OBJDIR)/*.d
-include $(TEST_OBJDIR)/*.d

.PHONY : clean 
clean :	
	-rm -fr $(OBJDIR)
	-rm -fr $(TEST_OBJDIR)
