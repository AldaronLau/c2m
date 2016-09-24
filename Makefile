default: c2m
	clang -O3 src/main.c -o c2m -ISDL2-c2m/include -ldl -lm -lpthread

test: default
	cd test/ && ./../c2m
