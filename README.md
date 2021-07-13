# SOL-Progetto2021
Progetto per il laboratorio di Sistemi Operativi Laboratorio dell'a.a. 2020-2021

## Compilazione
Una volta scaricato il codice è altamente consigliato effettuare un `make cleanall` e successivamente `make all` per compilare.
## Esecuzione
Controllare che i file all'interno della cartella script abbiano i permessi di esecuzione. Se così non fosse, utilizzare all'interno della cartella script il comando `chmod +x *`.
Per eseguire i test digitare da dentro la cartella "SOL-Progetto2021" il comando "make test1" per eseguire il primo test e "make test2" per eseguire il secondo test.
I file restituiti dal Server saranno presenti nella cartella "tmp"
## Funzionalità facoltative implementate
* `lockFile`
* `unlockFile`
* flag `-D`
* log del server su un file
* script statistiche.sh
