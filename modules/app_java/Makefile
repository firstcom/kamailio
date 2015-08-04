# 
# WARNING: do not run this directly, it should be run by the master Makefile

include ../../Makefile.defs
auto_gen=
NAME=app_java.so

#DEFS += -DEXTRA_DEBUG
# These two lines is borrowed from FreeSWITCH mod_java, and is a very good list of possible java directories
java_dirs=/usr /usr/local /usr/lib/j2sdk1.4-sun /usr/lib/jvm/java /usr/lib/jvm/default-java /opt /mingw
java_inc_dirs=include include/libgcj

ifeq ($(JAVA_HOME),)
	# Loop through possible java paths
	java_detect_home = $(foreach cur_ic_dir,$(java_inc_dirs),$(shell if [ -f $(dir)/$(cur_ic_dir)/jni.h ]; then echo $(dir); fi))
	JAVA_HOME = $(strip $(foreach dir,$(java_dirs), $(java_detect_home)))
endif

ifeq ($(JAVA_HOME),)
	ifeq ($(OS), darwin)
		JAVA_HOME = $(shell /usr/libexec/java_home)
	else
		JAVA_HOME = $(shell readlink -f /usr/bin/javac | sed "s:bin/javac::")
	endif
endif

ifeq ($(JAVA_HOME),)
   $(error Can't locate Java Development Kit. You have to specify environment JAVA_HOME to build app_java)
endif

DEFS += -I$(JAVA_HOME)/include -I$(JAVA_HOME)/include/$(OS)

# For some reason, Java on OSX doesnt use achitecture in its libjvm path
ifeq ($(OS),darwin)
LIBS += -L$(JAVA_HOME)/jre/lib/server
else
LIBS += -L$(JAVA_HOME)/jre/lib/$(HOST_ARCH)/server
endif

# disable optimisation for segfaults debugging
INCLUDE += -O0 -g
INCLUDES += -O0 -g

DEFS+=-DKAMAILIO_MOD_INTERFACE

include ../../Makefile.modules

