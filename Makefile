FILES = ext2_mkdir ext2_cp ext2_rm ext2_rm_bonus ext2_restore ext2_ln ext2_checker
HELPERS = ext2_helper.c ext2_helper_modified.h
CFLAGS = -Wall -g -std=gnu99

all: ${FILES}

ext2_mkdir: ext2_mkdir.c ${DEPS}
	gcc ${CFLAGS} -o ext2_mkdir ext2_mkdir.c 

ext2_cp: ext2_cp.c ${DEPS}
	gcc ${CFLAGS} -o ext2_cp ext2_cp.c 

ext2_rm: ext2_rm.c ${DEPS}
	gcc ${CFLAGS} -o ext2_rm ext2_rm.c 

ext2_rm_bonus: ext2_rm_bonus.c ${DEPS}
	gcc ${CFLAGS} -o ext2_rm_bonus ext2_rm_bonus.c 

ext2_restore: ext2_restore.c ${DEPS}
	gcc ${CFLAGS} -o ext2_restore ext2_restore.c 

ext2_ln: ext2_ln.c ${DEPS}
	gcc ${CFLAGS} -o ext2_ln ext2_ln.c 

ext2_checker: ext2_checker.c ${DEPS}
	gcc ${CFLAGS} -o ext2_checker ext2_checker.c 

clean:
	rm -f ${FILES}