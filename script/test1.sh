#!/bin/bash
echo -e "\nEsecuzione del server con storage:\n10000 file\n128MB\nThreads worker: 1\n"
valgrind --leak-check=full ./bin/Server ./tmp/test1.txt &
PID=$!
sleep 1

echo -e "\n\tEsecuzione Client 1\n"
./bin/Client -f tmp/socket.sk -w TestFolder/Canzoni,n=0 -D tmp -W TestFolder/prova.txt,TestFolder/file.txt -D tmp -l TestFolder/Canzoni/LaVieEnRose.txt,TestFolder/prova.txt -u TestFolder/Canzoni/LaVieEnRose.txt -p -t 200
echo -e "\n\tEsecuzione Client 2\n"
./bin/Client -f tmp/socket.sk -R n=3 -d tmp -l TestFolder/Canzoni/LaVieEnRose.txt -r TestFolder/Canzoni/LaVieEnRose.txt -d tmp -W TestFolder/img/basket/DRose.jpg -D tmp -W TestFolder/img/NewYorkCity.jpg -p -t 200
echo -e "\n\tEsecuzione Client 3\n"
./bin/Client -f tmp/socket.sk -l TestFolder/Canzoni/LaVieEnRose.txt -c TestFolder/Canzoni/LaVieEnRose.txt -p -t 200

kill -SIGHUP ${PID}
sleep 1