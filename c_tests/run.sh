cd "$(dirname "$0")"

# -D_GNU_SOURCE is a Libuv compilation work-around
# https://github.com/joyent/libuv/issues/551
gcc -std=c99 -D_GNU_SOURCE\
    -Wall -Werror -pedantic \
    -lcheck -luv \
    -O3 -g \
    ../rig_c_scp/rs*.c -I../rig_c_scp \
    *.c \
    -o test_rig_scp && \
	valgrind -q --leak-check=full ./test_rig_scp
