#!/bin/bash
echo -e "\nEsecuzione del server con storage:\10 file\n1MB\nThreads worker: 4\n"
./bin/Server ./tmp/test2.txt &
PID=$!
sleep 1

echo -e "\n\tEsecuzione Client 1\n"
./bin/Client -f tmp/socket.sk -W TestFolder/prova.txt,TestFolder/file.txt -D tmp -w TestFolder/Canzoni,n=0 -D tmp -w TestFolder/test2 -D tmp -p
echo -e "\n\tEsecuzione Client 2\n"
./bin/Client -f tmp/socket.sk -w TestFolder/img,n=2 -D tmp -W TestFolder/img/NewYorkCity.jpg -p
echo -e "\n\tEsecuzione Client 3\n"
./bin/Client -f tmp/socket.sk -l TestFolder/img/NewYorkCity.jpg -c TestFolder/img/NewYorkCity.jpg -W TestFolder/img/basket/Leonard.jpg -D tmp -p

kill -SIGHUP ${PID}
sleep 1