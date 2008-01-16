#!/bin/sh

g15new

g15box 0,0,160,43,1,1,0
g15flush
sleep 1
g15box 0,0,160,43,1,1,0
g15line 0,0,160,43,1
g15line 160,0,0,43,1
g15flush
sleep 1
g15box 0,0,159,43,1,1,1
g15flush
sleep 1
g15clear
g15circle 20,20,20,0,1
g15circle 70,20,20,1,1
g15circle 120,20,20,0,1
g15flush
g15clear
sleep 1
g15print "percentage bar",0,1,1,1,i
for i in `seq 1 10`; do
	g15bar 1,20,160,30,1,$i,10,0
	g15flush
	sleep .2
done
g15flush
sleep 1
g15clear
for s in `seq 0 2`; do
	for i in `seq 0 5`; do
		g15print "row $i",$i,$s,1,1,cx
	done
	g15flush
	sleep 1;
	g15clear
done
sleep 1
g15clear
g15print "inverse",2,2,1,1,i
g15flush 1
sleep 1
g15clear
g15print "centered",2,2,1,1,c
g15flush
sleep 1
g15clear
g15print "centered inverse",2,2,1,1,ci
g15flush
sleep 1
g15clear
g15print "fullwidth centered",2,2,1,1,fc
g15flush
sleep 1
g15clear
g15print "round centered",2,2,1,1,rc
g15flush
sleep 1
g15clear
g15print "full round centered",2,2,1,1,frc
g15flush
sleep 1
g15clear

g15print "Goodbye",1,0,1,1,cx
g15print "Goodbye",2,1,1,1,cx
g15print "Goodbye",3,2,1,1,cx
g15flush

sleep 5
g15quit
