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
TESTDIR = testsrc
EXTRAFLAGS = -g -I./include -lcrypto -lrt -ljson-c
OBJDIR = obj

vpath %.c $(SUBDIRS)
vpath %.c $(COMMDIRS)
vpath %.c $(TESTDIR)


srcs = $(filter-out main.c, $(notdir $(wildcard $(SUBDIRS)/*.c)))
objs = $(srcs:%.c=$(OBJDIR)/%.o)

test_srcs = $(notdir $(wildcard $(TESTDIR)/*.c))
test_objs = $(test_srcs:%.c=$(OBJDIR)/%.o)
test_exec = $(test_srcs:%.c=%)

ifeq ($(DEBUG),1)
	TARGET_DIR=Debug
else
	TARGET_DIR=Release
endif

all : $(PACKAGE)
.PHONY : all
.PHONY : test

$(PACKAGE) : $(objs) 
	@mkdir -p $(TARGET_DIR)
	$(CC) -c $(CCFLAGS) $(EXTRAFLAGS) $(SUBDIRS)/main.c -o $(OBJDIR)/main.o
	$(CC) $(CCFLAGS) $(objs) $(common_objs) $(OBJDIR)/main.o $(EXTRAFLAGS) -o $(TARGET_DIR)/$(PACKAGE)

$(objs) : $(OBJDIR)/%.o: %.c
	@mkdir -p $(OBJDIR)
	$(CC) -MMD -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@ 

$(common_objs) : $(OBJDIR)/%.o: %.c
	$(CC) -MMD -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@ 

test : $(objs) $(test_objs)
	@mkdir -p $(TARGET_DIR)
	$(foreach test_target, $(test_exec), $(CC) $(CCFLAGS) $(EXTRAFLAGS) $(objs) -o $(TARGET_DIR)/$(test_target)  $(OBJDIR)/$(test_target).o ;)
$(test_objs) : $(OBJDIR)/%.o: %.c
	$(CC) -c $(CCFLAGS) $(EXTRAFLAGS) $< -o $@

#check header for obj reconstruction
-include $(OBJDIR)/*.d

.PHONY : clean 
clean :	
	-rm -fr $(OBJDIR)/* $(TARGET_DIR)/*
	-rm -fr $(TARGET_DIR)
	-rm -fr $(test_exec) 
