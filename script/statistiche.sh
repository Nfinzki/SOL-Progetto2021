#!/bin/bash

numRead=0
numWrite=0
numLock=0
numOpenLock=0
numUnlock=0
numClose=0
numReplace=0

writtenB=0
readB=0

maxSize=0
maxFile=0
maxConn=0

logFile=$1
#Controlla se il file esiste
if [ -e ${logFile} ]; then
    #Controlla se il file non è vuoto
    if [ -s ${logFile} ]; then
        #Conta il numero di letture
        numRead=$(grep "\[READ\]" "${logFile}" | wc -l)

        #Conta il numero di scritture
        numWrite=$(grep "\[WRITE\]" "${logFile}" | wc -l)

        #Conta il numero di lock
        numLock=$(grep "\[LOCK\]" "${logFile}" | wc -l)

        #Conta il numero di open lock
        numOpenLock=$(grep "\[OPEN_LOCK\]" "${logFile}" | wc -l)

        #Conta il numero di unlock
        numUnlock=$(grep "\[UNLOCK\]" "${logFile}" | wc -l)

        #Conta il numero di close
        numClose=$(grep "\[CLOSE\]" "${logFile}" | wc -l)

        #Conta il numero di volte in cui è stato eseguito l'algoritmo di sostituzione
        numReplace=$(grep "\[REPLACEMENT POLICY\] New execution" "${logFile}" | wc -l)

        #Calcola la massima dimensione in MB raggiunta dallo storage
        maxSize=$(grep "\[STORAGE SIZE\]" "${logFile}" | cut -d" " -f7 | sort -rg | head -n1)

        #Calcola il massimo numero di file raggiunto dallo storage
        maxFile=$(grep "\[STORAGE FILE\]" "${logFile}" | cut -d" " -f7 | sort -rg | head -n1)

        #Calcola il massimo numero di connessioni contemporanee
        maxConn=$(grep "\[CONNECTION\]" "${logFile}" | cut -d" " -f6 | sort -rg | head -n1)

        #Costruisco la stringa per calcolare la somma dei byte letti
        for i in $(grep "\[READ\]" "${logFile}" | cut -d" " -f5); do
            readB=$readB+$i;
        done
        #Calcolo la somma
        readB=$(bc <<< ${readB})

        #Costruisco la stringa per calcolare la somma dei byte scritti
        for i in $(grep "\[WRITE\]" "${logFile}" | cut -d" " -f5); do
            writtenB=$writtenB+$i;
        done
        #Calcolo la somma
        writtenB=$(bc <<< ${writtenB})

        #Converto la dimensione massima raggiunta in MBytes
        if [ ${maxSize} != 0 ]; then
            maxSize=$(echo "scale=5; ${maxSize} / ( 1024 * 1024 )" | bc -l)
        fi


        #Stampo le statistiche
        echo -e "\n\tStatistiche ${logFile}:"

        echo "Numero di read: ${numRead}"
        if [ ${numRead} != 0 ]; then
            echo "Numero medio di byte letti: $(echo "scale=2; ${readB} / ${numRead}" | bc)"
        fi

        echo "Numero di write: ${numWrite}"
        if [ ${numWrite} != 0 ]; then
            echo "Numero medio di byte letti: $(echo "scale=2; ${writtenB} / ${numWrite}" | bc)"
        fi

        echo "Numero di lock: ${numLock}"
        echo "Numero di open-lock: ${numOpenLock}"
        echo "Numero di unlock: ${numUnlock}"
        echo "Numero di close: ${numClose}"
        echo "Dimensione massima in Mbytes raggiunta dallo storage: ${maxSize}"
        echo "Dimensione massima in numero di file raggiunta dallo storage: ${maxFile}"
        echo "Numero di volte in cui l'algoritmo di rimpiazzamento della cache è stato eseguito per selezionare un file vittima: ${numReplace}"

        for i in $(grep "\[REQUEST\]" "${logFile}" | cut -d" " -f4 | sort -g | uniq); do
            echo "Numero di richieste servite dal thread ${i}: $(grep "\[REQUEST\] ${i}" "${logFile}" | wc -l)"
        done

        echo "Massimo numero di connessioni contemporanee: ${maxConn}"
    
    else
        echo "${logFile}: Il file di log è vuoto"
    fi
else
    echo "${logFile}: Il file non esiste oppure non è un file"
fi