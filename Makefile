main:
	mpicxx -std=c++11 -Wall -o main main.cpp verbs_wrap.cpp -libverbs

verbs_wrap:
	mpicxx -std=c++11 -c -Wall -o verbs_wrap.o verbs_wrap.cpp
