DEBUG ?= 1
CC = gcc
CCFLAGS = -g -O2 -std=gnu99 
CPPFLAGS = 
CXX = g++
CXXFLAGS = $(CCFLAGS)
PACKAGE = minicached
PACKAGE_NAME = $(PACKAGE)
PACKAGE_STRING = $(PACKAGE_NAME)1.0
PACKAGE_VERSION = 1.0
SHELL = /bin/sh
VERSION = 1.0
SUBDIRS = source
COMMDIRS = ../common
TESTDIR = test
EXTRAFLAGS = -g -I./include -lcrypto -lrt
OBJDIR = obj

vpath %.c $(SUBDIRS)
vpath %.c $(COMMDIRS)
vpath %.c $(TESTDIR)


srcs = $(filter-out main.c, $(notdir $(wildcard $(SUBDIRS)/*.c)))
objs = $(srcs:%.c=$(OBJDIR)/%.o)

ifeq ($(DEBUG),1)
	TARGET_DIR=Debug
else
	TARGET_DIR=Release
endif

all : $(PACKAGE)
.PHONY : all
.PHONY : test

$(PACKAGE) : $(objs) 
	$(CC) -c $(CCFLAGS) $(EXTRAFLAGS) $(SUBDIRS)/main.c -o $(OBJDIR)/main.o
	$(CC) $(CCFLAGS) $(objs) $(common_objs) $(OBJDIR)/main.o $(EXTRAFLAGS) -o $(TARGET_DIR)/$(PACKAGE)

$(objs) : $(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	$(CC) -MMD -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@ 

$(common_objs) : $(OBJDIR)/%.o: %.c
	$(CC) -MMD -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@ 

#check header for obj reconstruction
-include $(OBJDIR)/*.d

.PHONY : clean 
clean :	
	-rm -fr $(OBJDIR)/* $(TARGET_DIR)/*
	-rm -fr $(test_exec) 
