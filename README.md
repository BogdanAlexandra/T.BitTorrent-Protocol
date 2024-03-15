Tema 3 - Algoritmi Paraleli si Distribuiti
-----------------------------------------------------------

Student: Bogdan Alexandra-Lacramioara
Grupa: 334CD

Structuri folosite
------------------------------------

typedef struct {
    char id; //id-ul clientului
    int type; //daca este peer sau seed
    int are_segment[MAX_CHUNKS]; // pentru a retine segmentele, daca  are clientul 
} Client; //structura folosita pentru a retine informatiile despre client


typedef struct {
    char filename[MAX_FILENAME]; //numele
    char hash[MAX_CHUNKS][HASH_SIZE + 1]; //hash-ul
    int number_of_chunks; //numarul de bucati/parti
} FileSegment; // //structura folosita pentru a retine informatiile despre un fisier

typedef struct {
    Client clients_list[MAX_CLIENTS]; //lista de clienti
    FileSegment file; //fisierul
    int num_clients; //numarul clientilor
} Swarm;  //structura folosita pentru a retine informatiile despre swarn

typedef struct {
    int rank; //rank-ul
    int number_of_wanted_files; //numar de fisiere de care este interesat
    int number_of_owned_files; //numar de fisiere detinute
    FileSegment *wanted_files; // fisiere de care este interesat
    FileSegment *owned_files;  //fisiere detinute
} ThreadInfo;  //structura folosita pentru a retine informatiile despre thread


Descrierea solutiei
-------------------------------------

    int randomPeer(Swarm* swarm_info, int segment, int rank) 
    ----------------------------------------------------------------
        Funcția randomPeer facilitează selecția aleatoare a unui client dintr-un Swarm în funcție de segmentul specificat.
        Funcția utilizează funcția rand() cu srand((unsigned int)time(NULL)) pentru a genera numere aleatoare.
        Alocă dinamic memorie pentru a stoca identificatorii clienților cu segmentul specificat.
        Returnează identificatorul unui client selectat aleator care are segmentul specificat.
        Dacă niciun client nu are segmentul specificat, funcția returnează -1.

    void sendUpdateToTracker(const char* message, const char* filename, const Client* clients_list) 
    ----------------------------------------------------------------------------------------------------

        Această funcție are rolul de a trimite un mesaj, un nume de fișier și o listă de clienți către un tracker într-un mediu de programare paralelă cu ajutorul MPI (Message Passing Interface).
        Mesajul și numele fișierului sunt trimise folosind MPI_Send.
        Dimensiunea șirului de caractere este calculată pentru a asigura trimiterea completă a datelor.
        Lista de clienți este trimisă în funcție de dimensiunea structurii Client.

    void receiveInfoFromTracker(Swarm* swarm_info) 
    ---------------------------------------------------

        Această funcție are rolul de a primi informații despre un Swarm de la un tracker într-un mediu de programare paralelă cu ajutorul MPI.
        Informațiile despre Swarm sunt primite folosind MPI_Recv.
        Dimensiunea datelor este specificată în funcție de dimensiunea structurii Swarm.
        Statusul MPI este ignorat cu ajutorul MPI_STATUS_IGNORE.

    void *download_thread_func(void *arg) 
    ----------------------------------------------------

        După primirea informațiilor de la tracker, firul de execuție inițiază descărcarea segmentelor de la clienți aleatori din swarm și actualizează informațiile despre client la fiecare 10 segmente descărcate.
        Fișierele descărcate sunt salvate local într-un format specific, iar informațiile despre acestea sunt trimise înapoi la tracker la finalul fiecărui fișier descărcat.
        Procesul se repetă până când toate fișierele dorite au fost descărcate și salvate local.
        La final, firul de execuție trimite un mesaj de finalizare la tracker pentru a anunța încheierea procesului de descărcare.
        Această funcție realizează gestionarea complexă a descărcării de segmente de fișiere într-un mediu de programare paralel folosind MPI și actualizează periodic tracker-ul cu informații despre progresul descărcării.


    void* upload_thread_func(void* arg) 
    -----------------------------------------------------


        Dacă este o cerere de segment, funcția extrage informațiile relevante din mesaj (nume fișier și segment dorit) și apelează funcția sendFileSegment pentru a trimite segmentul solicitat către clientul cerător.
        Dacă mesajul primit este "CLOSE" (comparat prin strncmp(received_buffer, "CLOSE", 5)), funcția returnează, semnalizând închiderea firului de execuție.
        Bucla se reia, așteptând următoarea cerere.
        Această funcție realizează gestionarea cererilor de segmente de fișiere primite de la alți clienți sau fire de execuție într-un mediu paralel MPI, iar în funcție de tipul cererii, trimite segmentele corespunzătoare sau se încheie dacă primește un mesaj de închidere.


    int findFileIndex(const Swarm *swarm, int num_files, const char *filename) 
    ----------------------------------------------------------------------------------

        Funcția findFileIndex are rolul de a găsi indexul unui fișier în cadrul unui swarm, pe baza numelui acestuia. Funcția primește trei parametri:

                - swarm: un pointer către un vector de structuri Swarm, reprezentând informații despre mai multe fișiere și clienți asociați acestora.
                - num_files: un număr întreg indicând câte fișiere sunt prezente în swarm.
                - filename: un pointer către un șir de caractere reprezentând numele fișierului căutat.
                
            Funcționarea funcției:
                Inițializează un ciclu for care parcurge fiecare element din vectorul swarm de la indexul 0 până la num_files - 1.
                Verifică dacă numele fișierului de la indexul curent (swarm[i].file.filename) coincide cu numele căutat (filename) utilizând funcția strcmp.
                Dacă găsește o potrivire, returnează indexul fișierului găsit.
                Dacă nicio potrivire nu este găsită după parcurgerea întregului vector, funcția returnează -1 pentru a indica că fișierul căutat nu există în swarm.


    int findClientIndex(Swarm swarm[], int file_index, int id)
    ------------------------------------------------------------------

        Această funcție are rolul de a găsi indexul unui client în cadrul listei de clienți asociată unui anumit fișier dintr-un swarm dat. Funcția primește trei parametri:

                -swarm: un vector de structuri Swarm reprezentând informații despre mai multe fișiere și clienții asociați acestora.
                -file_index: un index specificând fișierul pentru care se caută clientul în cadrul swarm-ului.
                -id: identificatorul clientului căutat.

            Funcționarea funcției:
                Inițializează un contor i la 0.
                Parcurge lista de clienți asociată fișierului cu indexul file_index în cadrul swarm-ului.
                Verifică dacă identificatorul (id) al clientului curent corespunde cu cel căutat.
                Incrementarea contorului i continuă până când se găsește clientul sau se parcurge întreaga listă.
                Returnează indexul clientului găsit sau -1 dacă clientul nu este găsit în lista asociată fișierului.

    void handleRequest(const Swarm *swarm, int num_files, int sender_rank, const char *requested_filename) 
    -------------------------------------------------------------------------------------------------------------

        Această funcție gestionează cereri de informații despre un swarm și trimite răspunsuri către un anumit rang MPI (proces). Funcția primește patru parametri:

            - swarm: un vector de structuri Swarm reprezentând informații despre mai multe fișiere și clienții asociați acestora.
            - num_files: numărul total de fișiere din swarm.
            - sender_rank: rangul MPI al procesului care a trimis cererea.
            - requested_filename: numele fișierului pentru care se solicită informații.

        Funcționarea funcției:
            Folosește funcția findFileIndex pentru a determina indexul fișierului în cadrul swarm-ului pe baza numelui cerut.
            Dacă fișierul este găsit (index diferit de -1), se trimite informația despre swarm-ul asociat acestuia la procesul care a solicitat, folosind MPI_Send.
            În caz contrar, nu se trimite niciun răspuns, deoarece fișierul cerut nu există în swarm.


    void handleUpdate(Swarm *swarm, int num_files, int sender_rank) 
    ---------------------------------------------------------------------
        Funcția handleUpdate gestionează actualizările trimise de un client sau un fir de execuție către un swarm într-un mediu de programare paralel cu MPI. Iată o descriere detaliată a funcției:

        Primește trei parametri:
            - swarm: Un pointer către un vector de structuri Swarm, reprezentând informații despre mai multe fișiere și clienți asociați acestora.
            - num_files: Un număr întreg indicând câte fișiere sunt prezente în swarm.
            - sender_rank: Rangul MPI al procesului care trimite actualizarea.

        Prin apelul funcției MPI_Recv, primește numele fișierului pentru care se face actualizarea din partea clientului sau firului de execuție (update_filename).
        Utilizează funcția findFileIndex pentru a găsi indexul fișierului în cadrul swarm-ului pe baza numelui primit.
        Dacă fișierul este găsit (index diferit de -1), continuă cu procesarea actualizării.
        Prin apelul funcției MPI_Recv, primește informații actualizate despre lista de clienți pentru fișierul respectiv (update_clients_list).
        Utilizează funcția findClientIndex pentru a găsi indexul clientului în cadrul listei de clienți asociată fișierului respectiv.
        Dacă clientul nu există în lista de clienți, adaugă informațiile despre noul client în cadrul swarm-ului și actualizează numărul total de clienți pentru fișierul respectiv.
        Dacă clientul există deja în lista de clienți, actualizează informațiile despre segmentele deținute de acesta.
        Prin apelul funcției MPI_Send, trimite înapoi actualizările către clientul sau firul de execuție care a trimis inițial cererea.


    void handleFinishFile(Swarm *swarm, int num_files, int sender_rank)
    ----------------------------------------------------------------------
        Această funcție se ocupă de procesarea notificărilor primite când un client sau un fir de execuție a finalizat descărcarea unui fișier din swarm. Iată o descriere a funcției:

        Primește trei parametri:
            - swarm: Un pointer către un vector de structuri Swarm, reprezentând informații despre mai multe fișiere și clienți asociați acestora.
            - num_files: Un număr întreg indicând câte fișiere sunt prezente în swarm.
            - sender_rank: Rangul MPI al procesului care trimite notificarea de finalizare a descărcării.

        Prin apelul funcției MPI_Recv, primește numele fișierului pentru care clientul sau firul de execuție a finalizat descărcarea (finished_filename).
        Utilizează funcția findFileIndex pentru a găsi indexul fișierului în cadrul swarm-ului pe baza numelui primit.
        Utilizează funcția findClientIndex pentru a găsi indexul clientului în cadrul listei de clienți asociată fișierului respectiv.
        Dacă clientul este găsit (index diferit de -1), actualizează tipul acestuia la 1 (indicând finalizarea descărcării) și afișează un mesaj de notificare.

    void handleFinishAllDownloads(int *finished, int numtasks) 
    -----------------------------------------------------------------------

        Această funcție gestionează finalizarea tuturor descărcărilor în cadrul sistemului distribuit. Iată o descriere a funcției:

        Primește doi parametri:
            - finished: Un pointer către un număr întreg care indică câte descărcări au mai rămas de finalizat.
            - numtasks: Numărul total de procese MPI (numărul total de clienți sau fire de execuție în sistem).
            
        Decrementeză valoarea indicată de finished (numărul de descărcări rămase de finalizat).
        Dacă nu mai există descărcări rămase (valoarea indicată de finished este mai mică sau egală cu 0), trimite mesajul "CLOSE" către toate celelalte procese MPI pentru a semnaliza închiderea acestora.

    void tracker(int numtasks, int rank)
    ---------------------------------------
        Funcția tracker reprezintă implementarea funcționalităților unui tracker într-un sistem distribuit care utilizează MPI. 

        Descriere functie:
            Inițializează variabile locale, inclusiv un vector de structuri Swarm pentru a stoca informații despre fișiere și clienți, un contor pentru numărul total de descărcări finalizate (finished), un contor pentru numărul total de fișiere (num_files), și un indicator pentru a opri executia tracker-ului (stop).
            Intră într-o buclă infinită pentru a aștepta mesaje MPI de la clienți sau fire de execuție.
            Prin apelul funcției MPI_Recv, primește mesaje de la orice sursă (MPI_ANY_SOURCE) cu eticheta 0 și determină rangul MPI al expeditorului (sender_rank).
            Verifică conținutul mesajelor primite pentru a gestiona actualizări, cereri de informații, notificări de finalizare a descărcărilor, și semnale pentru închiderea proceselor MPI.
            În funcție de conținutul mesajelor, apelează funcții specifice pentru a trata cereri (handleRequest), actualizări (handleUpdate), finalizări de descărcare pentru un anumit fișier (handleFinishFile), și finalizarea tuturor descărcărilor (handleFinishAllDownloads).
            Actualizează variabila stop pentru a monitoriza numărul de descărcări rămase de finalizat.
            La finalul buclei, tracker-ul trimite semnale de închidere către toate procesele MPI.
        Această funcție joacă rolul central în gestionarea comunicației dintre tracker și clienți/firuri de execuție într-un mediu distribuit, asigurând sincronizarea informațiilor despre swarm și finalizarea corectă a proceselor la finalul descărcărilor.


    void peer(int numtasks, int rank)
    ------------------------------------
        Funcția peer reprezintă implementarea comportamentului unui nod peer într-un sistem distribuit, unde fiecare peer este responsabil pentru descărcarea și încărcarea de fișiere în cadrul swarm-ului. 

        Descrierea funcției:
            Inițializează variabile locale, inclusiv numele fișierului de configurare specific fiecărui peer, un vector de structuri FileSegment pentru a stoca informații despre fișierele deținute, și un contor pentru numărul de fișiere deținute.
            Deschide fișierul de configurare specific peer-ului pentru a citi informații despre fișierele pe care le deține.
            Parsează informațiile din fișierul de configurare pentru a obține numărul de fișiere deținute și detaliile despre acestea (nume, număr de segmente, hash-uri).
            Trimite informațiile despre fiecare fișier deținut la tracker prin intermediul MPI, utilizând etichete specifice pentru nume, număr de segmente și hash-uri.
            Așteaptă semnalul de la tracker pentru a confirma înregistrarea fișierelor.
            După înregistrarea cu succes, primește informații suplimentare de la tracker, inclusiv semnale pentru începerea și finalizarea descărcărilor.
            Citeste din fișierul de configurare numărul de fișiere pe care le dorește să le descarce.
            Alocă memorie pentru a stoca informațiile despre fișierele dorite.
            Primește numele fișierelor dorite de la fișierul de configurare.
            Inițializează structura ThreadInfo care conține informații necesare pentru firele de execuție de download și upload.
            Creează două fire de execuție pentru descărcarea (download_thread_func) și încărcarea (upload_thread_func) de fișiere.
            Așteaptă finalizarea firelor de execuție de download și upload prin apelurile la pthread_join.



        