main:
	mpicxx -std=c++11 -Wall -o main main.cpp verbs_wrap.cpp -libverbs
