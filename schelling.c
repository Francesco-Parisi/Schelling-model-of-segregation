#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <mpi.h>
#include <time.h>
#include <locale.h>
#include <assert.h>

//Agenti
#define AGENT_X 'X'
#define AGENT_O 'O'
#define EMPTY ' '
#define EMPTY_CELL -1
#define SATISFIED 0
#define UNSATISFIED 1

//Setting della percentuale di popolazione degli Agenti X e O
#define PERC_O 33
#define PERC_X 33

//Setting della sogliax di soddisfazione
#define PERCENTAGE_SAT 33.333

//Righe e Colonne della MAtrice
#define ROWS 100
#define COLUMNS 100

//Setting del numero max di iterazioni
#define ITER 100

//Macro per definire il colore di ogni agente della matrice
#define PRINT_RED(str) printf("\x1b[1;31m %c \x1b[0m", str);
#define PRINT_BLUE(str) printf("\x1b[1;36m %c \x1b[0m", str);

//Struttura necessaria per la gestione delle celle vuote
typedef struct empty_cell{
    int row_index;    //Riga della cella vuota
    int column_index; //Colonna della cella vuota
} empty_cell;

//Struttura necessaria per gestire lo spostamento degli agenti
typedef struct move_agent {
    int dest_row;     //Riga in cui l'agente si sposta
    int dest_column;  //Colonna in cui l'agente si sposta
    char agent;
} move_agent;

//Struttura necessaria per la stampa delle informazioni finali ottenute dalla computazione
typedef struct info_agents{
    int tot_agents;   //Numero totale di agenti della matrice
    int tot_x_agents; //Numero totale di agenti X
    int tot_o_agents; //Numero totale di agenti O  
} info_agents;

//Firme delle funzioni implementate
char matrix_init(char *matrix, int pa_x, int pa_y);         
void print_matrix(int rows, int columns, char *matrix);     
void print_random_agent(char agent);                        
void print_init_info(int size);                             
int *rows_distribution(int rank, int rows, int size, int *rows_proc, int *displacements, int *sendcounts); 
void send_rows(int rank, int size, int initial_rows, char *sub_matrix, MPI_Comm communicator);
int *assess_agent_satisfaction(int rank, int size, int initial_rows, int total_rows, char *sub_matrix, int *unsatisfied_agents);
int is_satisfied(int rank, int size, int rows_size, int total_rows, int row, int column, char *sub_matrix);
empty_cell* find_empty_cells(int initial_rows, char* sub_matrix, int displacement, int *local_empty_cells);
void calculate_displacements_and_total(int size, int *num_global_empty_cells, int *displacements, int *num_total_empty_cells);
void shuffle_global_empty_cells(int rank, int num_total_empty_cells, empty_cell *global_empty_cells);
void calculate_empty_cells_per_process(int size, int num_total_empty_cells, int *empty_cells_per_process, int *global_unsatisfied_agents, int *displacements);
empty_cell *distribute_empty_positions(int rank, int size, int num_local_empty_cells, empty_cell *local_empty_cells, int *num_dest_cells, MPI_Datatype datatype, int unsatisfied_agents);
void defineEmptyCell(MPI_Datatype *);
void defineMoveAgent(MPI_Datatype *);  
int calculate_destination_process(int size, int *displacement, int *sendcounts, int row);
void move_agents(int rank, int size, int initial_rows, char *sub_matrix, int *move, empty_cell *destinations, int num_assigned_empty_cells, int *displacements, int *sendcounts, MPI_Datatype move_agent_type);
void sync(int rank, int size, int *num_elems_to_send_to, move_agent **data, char *sub_matrix, MPI_Datatype move_agent_type);
void print_final_info(info_agents *agents_info, int unsatisfied_count, float satisfaction_percentage);
void final_satisfation(int rank, int size, char *matrix);


int main(int argc, char **argv) {
    system("reset");

    //Variabili
    int rank, size;
    double start, end;

    char *matrix = NULL;
    char *sub_matrix = NULL;
    int *rows_process = NULL;
    int *sendcounts = NULL;
    int *displacements = NULL;
    int *move = NULL;

    int num_unsat_agents = 0;
    int num_empty_cells = 0;
    int num_local_empty_cells = 0;
    int num_dest_cells = 0;

    srand((unsigned)time(NULL) + 0);
    
    empty_cell empty;
    empty_cell *local_empty_cells = NULL;    
    empty_cell *destinations = NULL;

    info_agents agents;

    //Inizializzazione MPI
    MPI_Status status;
    MPI_Request request;
    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &size);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    // MPI Datatypes
    MPI_Datatype EMPTY_CELL_TYPE;
    defineEmptyCell(&EMPTY_CELL_TYPE);
    MPI_Datatype MOVE_AGENT_TYPE;
    defineMoveAgent(&MOVE_AGENT_TYPE);

    //Controllo se il numero di righe è minore del numero di processi
    if (ROWS < size && rank == 0) {
        printf("\033[1;31mError! There are more processes then ROWS!\x1b[0m\nROWS: %d, Processes: %d\n", ROWS, size);
        MPI_Abort(MPI_COMM_WORLD, MPI_ERR_COUNT);
    }

    start = MPI_Wtime();

    //Inizializzazione degli array
    rows_process =  calloc(size, sizeof(int));
    sendcounts = calloc(size, sizeof(int));
    displacements = calloc(size, sizeof(int));

    if (rank == 0) {
        matrix = malloc(ROWS * COLUMNS * sizeof(char *));

        print_init_info(size);
        printf("\n🚩 Initial Matrix:\n\n");
        matrix_init(matrix, PERC_O, PERC_X);
        print_matrix(ROWS, COLUMNS, matrix);
    }

    rows_distribution(rank, ROWS, size, rows_process, displacements, sendcounts);
    
    sub_matrix = malloc(rows_process[rank] * COLUMNS * sizeof(char *));
    MPI_Scatterv(matrix, sendcounts, displacements, MPI_CHAR, sub_matrix, rows_process[rank] * COLUMNS, MPI_CHAR, 0, MPI_COMM_WORLD);

    int total_rows = rows_process[rank];
    bool edge_process = (rank == 0 || rank == size - 1);
    int rows_to_deduct = edge_process ? 1 : 2;
    int initial_rows = total_rows - rows_to_deduct;

    for (int i = 0; i < ITER; i++) {

        send_rows(rank, size, initial_rows, sub_matrix, MPI_COMM_WORLD);
        move = assess_agent_satisfaction(rank, size, initial_rows, total_rows, sub_matrix, &num_unsat_agents); 
        local_empty_cells = find_empty_cells(initial_rows, sub_matrix, displacements[rank], &num_local_empty_cells);
        destinations = distribute_empty_positions(rank, size, num_local_empty_cells, local_empty_cells, &num_dest_cells, EMPTY_CELL_TYPE, num_unsat_agents);
        move_agents(rank, size, initial_rows, sub_matrix, move, destinations, num_dest_cells, displacements, sendcounts, MOVE_AGENT_TYPE);  // Sposto gli agenti
       
        MPI_Barrier(MPI_COMM_WORLD);

        free(move);
        free(local_empty_cells);
        free(destinations);
    }

    //Raccolta delle sottomatrici processate da ciascun processo nel processo root
    MPI_Gatherv(sub_matrix, sendcounts[rank], MPI_CHAR, matrix, sendcounts, displacements, MPI_CHAR, 0, MPI_COMM_WORLD);
    end = MPI_Wtime();
    MPI_Type_free(&EMPTY_CELL_TYPE);
    MPI_Type_free(&MOVE_AGENT_TYPE);
    MPI_Finalize();

    //Stampa della matrice finale e calcolo della soddisfazione ottenuta
    if (rank == 0){
        printf("\n🏁 Final Matrix:\n\n");
        print_matrix(ROWS,COLUMNS, matrix);
        printf("\n---------------------------------*\n");
        final_satisfation(rank, size, matrix);
        printf("\nTime: %f\n", end - start);
        printf("---------------------------------*\n");
    }

    free(matrix);
    free(rows_process);
    free(sub_matrix);
    free(sendcounts);
    free(displacements);

    return 0;
}

//Funzione che inizializza una matrice ROWS*COLUMNS con agenti X, O e ' '
char matrix_init(char *matrix, int pa_x, int pa_y){
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLUMNS; j++) {
                
                //Viene assegnato un numero casuale tra 1 e 100
                int number = rand()%100+1;
                if(number>0 && number<= PERC_O)
                    matrix[i * COLUMNS + j] = AGENT_O;
                else if(number>PERC_O && number<= (PERC_O+PERC_X))
                    matrix[i * COLUMNS + j] = AGENT_X;
                else
                    matrix[i * COLUMNS + j] = EMPTY;
        }
    }
}

//Funzione che stampa la matrice di agenti precedentemente inizializzata
void print_matrix(int rows, int columns, char *matrix){
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLUMNS; j++) {
            //Viene passato alla funzione il contenuto della cella corrente
            print_random_agent(matrix[i * COLUMNS + j]);
        }
        printf("\n");
    }
}

//Funzione che stampa il singolo agente riconosciuto
void print_random_agent(char agent){
    if(agent == AGENT_X)
        PRINT_BLUE(agent);
    if(agent == AGENT_O)
        PRINT_RED(agent);
    if(agent == ' ')
        printf("   ");
}

//Funzione che stampa le informazioni iniziali, come il numero di processori, iterazioni e l'orario di inizio della computazione
void print_init_info(int size){
    time_t now;
    struct tm *current_time;
    char time_string[100];
    
    //Stampa la data e l'orario corrente
    time(&now);
    current_time = gmtime(&now);

    //Setting del fuso orario
    current_time->tm_hour += 2;
    strftime(time_string, sizeof(time_string), "%d-%m-%Y %H:%M:%S", current_time);
    printf("\033[1m🔹Schelling's Model of Segregation🔹\033[0m\n");
    printf("\nComputation started at: %s\n", time_string);
    printf("Number of Processors: %d\n", size);
    printf("Number of Iterations: %d\n\n", ITER);

}

//Funzione che si occupa di calcolare le righe della matrice da distribuire ai vari processi
int *rows_distribution(int rank, int rows, int size, int *rows_proc, int *displacements, int *sendcounts) {

    //Numero di righe assegnate ad ogni processo
    int rows_send = rows / size;

    //Righe rimanenti quando non divisibile per size
    int rows_rest = rows % size;
    
    int displacement = 0;

    for(int i = 0; i < size; i++) {
        sendcounts[i] = rows_send * COLUMNS;
        rows_proc[i] = rows_send;

        //In caso di righe extra ogni processo riceve una riga finche tutte le righe non vengono distribuite equamente
        if (rows_rest > 0) {
            sendcounts[i] += COLUMNS;
            rows_proc[i]++;
            rows_rest--;
        }
        //Memorizza l'offset di inizio per ogni processo
        displacements[i] = displacement;

        //Aggiornamento dell'offset
        displacement += sendcounts[i];

        //Se il processo è quello root oppure l'ultimo, viene assegnata una riga extra, altrimenti due
        if(rank == 0 || rank == size - 1)
            rows_proc[i] += 1;
        else
            rows_proc[i] += 2;
    }

    return rows_proc;
}

//Funzione che consente ad ogni processo di scambiare righe con i suoi vicini
void send_rows(int rank, int size, int initial_rows, char *sub_matrix, MPI_Comm communicator) {

    //Determina il rank del processo superiore 
    int neighbour_up = (rank + 1) % size;

    //Determina il rank del processo inferiore
    int neighbour_down = (rank + size - 1) % size;

    //Calcola la posizione della riga finale nella sottomatrice del processo corrente
    int my_last_row_pos = (initial_rows - 1) * COLUMNS; 

    //Calcola la posizione dove il processo corrente dovrebbe ricevere la riga dal suo vicino inferiore
    int neighbour_down_row_pos = initial_rows * COLUMNS; 

    //Calcola la posizione dove il processo corrente dovrebbe ricevere la riga dal suo vicino superiore
    int neighbour_up_row_pos = (initial_rows + (rank == 0 ? 0 : 1)) * COLUMNS;  

    //Array utilizzato per tenere traccia delle operazioni non bloccanti
    MPI_Request requests[4];
    int request_count = 0;

    // Se il processo non è root invia la prima riga al vicino inferiore e riceve una riga dallo stesso vicino, che verrà memorizzata subito dopo l'ultima riga della sua sottomatrice
    if (rank != 0) {
        MPI_Isend(sub_matrix, COLUMNS, MPI_CHAR, neighbour_down, 99, communicator, &requests[request_count++]);
        MPI_Irecv(sub_matrix + neighbour_down_row_pos, COLUMNS, MPI_CHAR, neighbour_down, 99, communicator, &requests[request_count++]);
    }

    // Se il processo non è l'ultimo invia l'ultima riga al vicino superiore e riceve una riga dallo stesso vicino, che verrà memorizzata alla fine o subito dopo l'ultima riga, a seconda del rank
    if (rank != size - 1) {
        MPI_Isend(sub_matrix + my_last_row_pos, COLUMNS, MPI_CHAR, neighbour_up, 99, communicator, &requests[request_count++]);
        MPI_Irecv(sub_matrix + neighbour_up_row_pos, COLUMNS, MPI_CHAR, neighbour_up, 99, communicator, &requests[request_count++]);
    }

    //Si attende che tutte le operazioni non bloccanti siano completate 
    MPI_Waitall(request_count, requests, MPI_STATUSES_IGNORE);
}

//Funzione che valuta la soddisfazione degli agenti all'interno di sub_matrix
int *assess_agent_satisfaction(int rank, int size, int initial_rows, int total_rows, char *sub_matrix, int *unsatisfied_agents) {
    int *mat = malloc(initial_rows * COLUMNS * sizeof(int *));

    //Contatore degli agenti insoddisfatti, inizializzato a 0
    *unsatisfied_agents = 0;

    for (int i = 0; i < initial_rows; i++) {
        for (int j = 0; j < COLUMNS; j++) {
            int idx = i * COLUMNS + j;

            //Viene verificato se la cella non è vuota, in tal caso viene assegnato un valore intero (SATISFIED:0, UNSATISFIED:1)
            if (sub_matrix[idx] != EMPTY) {

                //Creazione di una matrice caratterizzata dal valore di soddisfazione di ogni agente, per ogni cella
                mat[idx] = is_satisfied(rank, size, initial_rows, total_rows, i * COLUMNS, j, sub_matrix) ? SATISFIED : UNSATISFIED;

                //Viene aggiornato il conteggio degli agenti insoddisfatti
                *unsatisfied_agents += (mat[idx] == UNSATISFIED) ? 1 : 0;
            }
            else {
                mat[idx] = EMPTY_CELL;
            }
        }
    }

    return mat;
}

//Funzione che determina la soddisfazione di un agente della matrice, in base ai suoi vicini
int is_satisfied(int rank, int size, int rows_size, int total_rows, int row, int column, char *sub_matrix) {
    int left_index, right_index;
    int ngh_precedent_row, ngh_next_row;

    //Array contenente i vicini dell'elemento corrente
    char neighbours[8];

    //Elemento corrente della sottomatrice
    char current_element = sub_matrix[row + column];

    int neighbours_count = 8;
    int similar = 0;

    //Vengono stabiliti gli indici dei vicini a sinistra e a destra, a meno che l'elemento corrente non si trovi sui bordi sinistro o destro della matrice
    left_index = (column != 0) ? column - 1 : -1;
    right_index = (column != COLUMNS - 1) ? column + 1 : -1;

    if (rank == 0) {
        //Prima sottomatrice, dunque non c'è una riga precedente
        ngh_precedent_row = -1;
        //La riga successiva è l'ultima
        ngh_next_row = total_rows * COLUMNS - COLUMNS;
    } else if (rank == size - 1) {
        //La riga precedente è la penultima
        ngh_precedent_row = total_rows * COLUMNS - 2 * COLUMNS;
        
        //Ultima sottomatrice, dunque non c'è una riga successiva
        ngh_next_row = -1;
    } else {
        //La riga precedente è la penultima
        ngh_precedent_row = total_rows * COLUMNS - 2 * COLUMNS;
        //La riga successiva è l'ultima
        ngh_next_row = total_rows * COLUMNS - COLUMNS;
    }

    //Se l'agente si trova sulla prima riga, non avrà una riga precedente, pertanto viene impostato a -1
    int prev_row = (row != 0) ? row - COLUMNS : -1;
    int curr_row = row;
    
    //Se l'agente si trova sull'ultima riga, non avrà una riga successiva, pertanto viene impostato a -1
    int next_row = (row != rows_size - 1) ? row + COLUMNS : -1;

    //Ricava il vicino superiore sinistro
    if (left_index != -1 && prev_row != -1) {
        // Se esiste una colonna a sinistra e una riga precedente nella sottomatrice corrente
        neighbours[0] = sub_matrix[prev_row + left_index];
    } else if (rank != 0 && left_index != -1) {
        // Se si è su un bordo tra processi e c'è una colonna a sinistra
        neighbours[0] = sub_matrix[ngh_precedent_row + left_index];
    } else {
        //Se nessun vicino superiore sinistro è disponibile
        neighbours[0] = '\0';
    }

    //Ricava il vicino superiore centrale
    if (prev_row != -1) {
        //Se esiste una riga precedente nella sottomatrice corrente
        neighbours[1] = sub_matrix[prev_row + column];
    } else if (rank != 0) {
        //Se si è su un bordo tra processi
        neighbours[1] = sub_matrix[ngh_precedent_row + column];
    } else {
        //Se nessun vicino superiorecentrale è disponibile
        neighbours[1] = '\0';
    }

    //Ricava il vicino superiore destro
    if (right_index != -1 && prev_row != -1) {
        //Se esiste una colonna a destra e una riga precedente nella sottomatrice corrente
        neighbours[2] = sub_matrix[prev_row + right_index];
    } else if (rank != 0 && right_index != -1) {
        //Se si è su un bordo tra processi e c'è una colonna a destra
        neighbours[2] = sub_matrix[ngh_precedent_row + right_index];
    } else {
        //Se nessun vicino superiore destro è disponibile
        neighbours[2] = '\0';
    }

    //Ricava il vicino centrale sinistro
    neighbours[3] = (left_index != -1) ? sub_matrix[curr_row + left_index] : '\0';

    //Ricava il vicino centrale destro
    neighbours[4] = (right_index != -1) ? sub_matrix[curr_row + right_index] : '\0';

    //Ricava il vicino inferiore sinistro
    if (left_index != -1 && next_row != -1) {
        //Se esiste una colonna a sinistra e una riga successiva nella sottomatrice corrente
        neighbours[5] = sub_matrix[next_row + left_index];
    } else if (rank != size - 1 && left_index != -1) {
        //Se si è su un bordo tra processi e c'è una colonna a sinistra
        neighbours[5] = sub_matrix[ngh_next_row + left_index];
    } else {
        //Se nessun vicino inferiore sinistro è disponibile
        neighbours[5] = '\0';
    }

    //Ricava il vicino inferiore centrale
    if (next_row != -1) {
        // Se esiste una riga successiva nella sottomatrice corrente
        neighbours[6] = sub_matrix[next_row + column];
    } else if (rank != size - 1) {
        //Se si è su un bordo tra processi
        neighbours[6] = sub_matrix[ngh_next_row + column];
    } else {
        //Se nessun vicino inferiore centrale è disponibile
        neighbours[6] = '\0';
    }

    //Ricava il vicino inferiore destro
    if (right_index != -1 && next_row != -1) {
        //Se esiste una colonna a destra e una riga successiva nella sottomatrice corrente
        neighbours[7] = sub_matrix[next_row + right_index];
    } else if (rank != size - 1 && right_index != -1) {
        //Se si è su un bordo tra processi e c'è una colonna a destra
        neighbours[7] = sub_matrix[ngh_next_row + right_index];
    } else {
        //Se nessun vicino inferiore destro disponibile
        neighbours[7] = '\0';
    }

    //Viene controllato ogni vicino e confrontato ciascuno con l'agente corrente
    for (int i = 0; i < 8; i++) {
        //Se i vicini sono simili viene incrementato il contatore, altrimenti decremento
        if (neighbours[i] == current_element) similar++;
        else if (neighbours[i] == '\0') neighbours_count--;
    }

    //Calcolo della percentuale dei vicini simili. Se maggiore della soglia ritorna 1, altrimenti 0
    return ((((double)100 / neighbours_count) * similar) >= PERCENTAGE_SAT) ? 1 : 0;
}

//Funzione che cerca le celle vuote in una sottomatrice e restituisce una array di strutture che indicano le coordinate delle celle 
empty_cell* find_empty_cells(int initial_rows, char* sub_matrix, int displacement, int *local_empty_cells) {
    empty_cell *emptyCells = malloc(initial_rows * COLUMNS * sizeof(empty_cell));

    //Puntatore utilizzato per tenere traccia della posizione corrente della cella
    empty_cell *currentCell = emptyCells;

    // Ricerca delle celle vuote nella sottomatrice, cercando in ogni singola cella
    for (int i = 0; i < initial_rows; i++) {
        for (int j = 0; j < COLUMNS; j++) {
            if (sub_matrix[i * COLUMNS + j] == EMPTY) {
                //Viene impostato l'indice della riga e della colonna per la cella corrente
                currentCell->row_index = displacement + i * COLUMNS;
                currentCell->column_index = j;
                currentCell++;
            }
        }
    }

    //Ridimensionamento di emptyCells per adattarsi al numero di celle effettivamente vuote
    int foundCells = currentCell - emptyCells;
    emptyCells = realloc(emptyCells, foundCells * sizeof(empty_cell));
    *local_empty_cells = foundCells;

    return emptyCells;
}


//Funzione che calcola i displacements e recupera il totale delle celle vuote
void calculate_displacements_and_total(int size, int *num_global_empty_cells, int *displacements, int *num_total_empty_cells) {
    for (int i = 0; i < size; i++) {
        //Se il processo è root, displacement è settato a 0, altrimenti displacement è la somma dello spostamento del processo precedente e del numero di celle vuote
        displacements[i] = i == 0 ? 0 : displacements[i - 1] + num_global_empty_cells[i - 1];
        *num_total_empty_cells += num_global_empty_cells[i];
    }
}

//Funzione che mescola l'array di celle vuote in modo casuale
void shuffle_global_empty_cells(int rank, int num_total_empty_cells, empty_cell *global_empty_cells) {
    //Lo shuffle viene fatto utilizzando un numero fisso per assicurare che l'ordine casuale sia sempre lo stesso 
    srand(15);

    for (int i = 0; i < num_total_empty_cells; i++) {
        //Per ogni cella viene generato un indice casuale tra 0 e num_total_empty_cells-1
        int destination = rand() % num_total_empty_cells;
        
        //Viene scambiata la cela corrente con la cella relativa all'indice casuale
        empty_cell tmp = global_empty_cells[destination];
        global_empty_cells[destination] = global_empty_cells[i];

        //Array con ordine casuale delle celle vuote
        global_empty_cells[i] = tmp;
    }
}

//Funzione che calcola le celle vuote in base al numero di agenti insoddisfatti che ciascun processo gestisce
void calculate_empty_cells_per_process(int size, int num_total_empty_cells, int *empty_cells_per_process, int *global_unsatisfied_agents, int *displacements) {
    //Numero medio di celle vuote che devono essere distribuite tra processi
    int division = num_total_empty_cells / size;

    //Resto delle celle vuote dopo la distribuzione equa tra processi
    int rest = num_total_empty_cells % size;

    int displacement = 0;
    for (int i = 0; i < size; i++) {

        empty_cells_per_process[i] = division > global_unsatisfied_agents[i] ? global_unsatisfied_agents[i] : division;
        
        //In caso di eccesso di celle vuote non assegnate equamente e il numero di celle vuote assegnato al processo corrente non è maggiore del numero di agenti insoddisfatti
        if (rest > 0 && !(division > global_unsatisfied_agents[i])) {

            //Incremento del numero di celle vuote per tale processo e decremento il resto  
            empty_cells_per_process[i]++;
            rest--;
        }

        //Setto lo spostamento per il processo corrente e aggiorno il valore di displacement
        displacements[i] = displacement;
        displacement += empty_cells_per_process[i];
    }
}

//Funzione che distribuisce le posizioni vuote tra i vari processi
empty_cell *distribute_empty_positions(int rank, int size, int num_local_empty_cells, empty_cell *local_empty_cells, int *num_dest_cells, MPI_Datatype datatype, int unsatisfied_agents) {
    
    //Array di celle vuote per ogni processo
    int num_global_empty_cells[size];
    
    //Array per indicare gli offset
    int displacements[size];

    int num_total_empty_cells = 0;

    //Array contenente tutte le celle vuote di tutti i processi
    empty_cell *global_empty_cells = malloc(ROWS * COLUMNS * sizeof(empty_cell));
    
    //Array contenente il numero di agenti insoddisfatti per ogni processo
    int global_unsatisfied_agents[size];

    //Array che indica il numero di celle vuote che ogni processo dovrebbe ricevere dopo la redistribuzione.
    int *empty_cells_per_process = malloc(ROWS * COLUMNS * sizeof(empty_cell));

    //Per ogni processo viene raccolto il numero di celle vuote locali e vengono memorizzate nell'array num_global_empty_cells
    MPI_Allgather(&num_local_empty_cells, 1, MPI_INT, num_global_empty_cells, 1, MPI_INT, MPI_COMM_WORLD);
    
    calculate_displacements_and_total(size, num_global_empty_cells, displacements, &num_total_empty_cells);
    
    //Per ogni processo vengono raccolte le posizioni delle celle vuote e memorizzate in global_empty_cells
    MPI_Allgatherv(local_empty_cells, num_local_empty_cells, datatype, global_empty_cells, num_global_empty_cells, displacements, datatype, MPI_COMM_WORLD);
    
    //Per ogni processo viene raccolto il numero di agenti insoddisfatti e memorizzato in global_unsatisfied_agents 
    MPI_Allgather(&unsatisfied_agents, 1, MPI_INT, global_unsatisfied_agents, 1, MPI_INT, MPI_COMM_WORLD);
    
    shuffle_global_empty_cells(rank,num_total_empty_cells, global_empty_cells);
    calculate_empty_cells_per_process(size, num_total_empty_cells, empty_cells_per_process, global_unsatisfied_agents, displacements);
    *num_dest_cells = empty_cells_per_process[rank];
    
    //Array di celle vuote che il processo corrente deve ricevere
    empty_cell *empty_cell_current_proc = malloc(sizeof(empty_cell) * empty_cells_per_process[rank]);
    
    //Distribuzione delle posizioni vuote tra i vari processi
    MPI_Scatterv(global_empty_cells, empty_cells_per_process, displacements, datatype, empty_cell_current_proc, empty_cells_per_process[rank], datatype, 0, MPI_COMM_WORLD);

    free(global_empty_cells);
    free(empty_cells_per_process);

    return empty_cell_current_proc;
}

//Funzione che definisce un nuovo tipo di dato basato sulla struct empty_cell
void defineEmptyCell(MPI_Datatype *emptyCellType) {
    // Numero di elementi della struct
    int itemCount = 2;

    //Lunghezza dei blocchi, 1 perchè sono solo due interi
    int blockLengths[2] = {1, 1};
    MPI_Aint offsets[2];

    //Definizione dei tipi dei membri della struct
    MPI_Datatype dataTypes[2] = {MPI_INT, MPI_INT};
    
    empty_cell emptyCell;

    //MPI_Get_address recupera gli indirizzi dei membri della struct
    MPI_Get_address(&emptyCell.row_index, &offsets[0]);
    MPI_Get_address(&emptyCell.column_index, &offsets[1]);

    // Calcolo degli offset di ciascun membro della struct
    MPI_Aint baseAddress;
    MPI_Get_address(&emptyCell, &baseAddress);
    offsets[0] -= baseAddress;
    offsets[1] -= baseAddress;

    //Creazione e registrazione del tipo di dato personalizzato
    MPI_Type_create_struct(itemCount, blockLengths, offsets, dataTypes, emptyCellType);
    MPI_Type_commit(emptyCellType);
}

//Funzione che definisce un nuovo tipo di dato basato sulla struct move_agent
void defineMoveAgent(MPI_Datatype *moveAgentType) {
    // Numero di elementi della struct
    const int itemCount = 3;
    
    //Lunghezza dei blocchi, 1 perchè sono solo due interi e un char
    int blockLengths[3] = {1, 1, 1};
    MPI_Aint offsets[3];

    //Definizione dei tipi dei membri della struct
    MPI_Datatype dataTypes[3] = {MPI_INT, MPI_INT, MPI_CHAR};
    
    move_agent moveAgent;
    
    //Calcola e memorizza l'indirizzo di base per la struct
    MPI_Aint base_address;
    MPI_Get_address(&moveAgent, &base_address);
    
    //MPI_Get_address recupera gli indirizzi dei membri della struct
    MPI_Get_address(&moveAgent.dest_row, &offsets[0]);
    MPI_Get_address(&moveAgent.dest_column, &offsets[1]);
    MPI_Get_address(&moveAgent.agent, &offsets[2]);
    
    //Converte gli indirizzi assoluti dei campi della struct in offset, necessari per la definizione del nuovo tipo di dato 
    for (int i = 0; i < itemCount; i++) {
        offsets[i] = MPI_Aint_diff(offsets[i], base_address);
    }

    //Creazione e registrazione del tipo di dato personalizzato
    MPI_Type_create_struct(itemCount, blockLengths, offsets, dataTypes, moveAgentType);
    MPI_Type_commit(moveAgentType);
}

//Funzione che calcola il processo destinatario per una determinata riga della matrice
int calculate_destination_process(int size, int *displacement, int *sendcounts, int row) {
    for (int tmp_rank = 0; tmp_rank < size; tmp_rank++) {

        //Inizio del range di righe assegnate ad un certo processo
        int start = displacement[tmp_rank] / COLUMNS;

        //Fine del range di righe assegnate
        int finish = start + sendcounts[tmp_rank] / COLUMNS;

        //Se la riga rientra nel range tra start e finish, viene restituito il processo destinatario, altrimenti 0
        if (row >= start && row < finish) {
            return tmp_rank;
        }
    }
    return 0;
}

//Funzione che gestisce lo spostamento degli agenti nella matrice
void move_agents(int rank, int size, int initial_rows, char *sub_matrix, int *move, empty_cell *destinations, int num_assigned_empty_cells, int *displacements, int *sendcounts, MPI_Datatype move_agent_type) {
    
    //Array che terrà traccia di quanti agenti il processo corrente deve inviare a ciascun altro processo
    int *num_elems_to_send_to = (int *)calloc(size, sizeof(int));

    //Matrice in cui ogni riga rappresenta i dati da inviare a un processo specifico
    move_agent **data = (move_agent **)malloc(sizeof(move_agent *) * size);

    //Inizializza ciascun elemento dell'array per conservare tutti gli agenti che potrebbero essere inviati a un altro processo
    for (int i = 0; i < size; i++) {
        data[i] = (move_agent *)malloc(sizeof(move_agent) * num_assigned_empty_cells);
    }

    //Scorro tutte le celle della sottomatrice
    for (int i = 0, used_empty_cells = 0; i < initial_rows && used_empty_cells < num_assigned_empty_cells; i++) {
        for (int j = 0; j < COLUMNS && used_empty_cells < num_assigned_empty_cells; j++) {
            int current_index = i * COLUMNS + j;

            //Se l'agente nella cella corrente non deve essere spostato allora salta all'iterazione successiva
            if (move[current_index] != 1) continue;

            //Cella di destinazione per l'agente corrente
            empty_cell destination = destinations[used_empty_cells];
            
            //Calcola quale processo dovrebbe ricevere l'agente in movimento
            int receiver = calculate_destination_process(size, displacements, sendcounts, destination.row_index / COLUMNS);
            
            //Calcola la riga di inizio e la riga di destinazione nella sotto-matrice del processo ricevente.
            int startRow = displacements[receiver];
            int destRow = destination.row_index - startRow;


            //Se il processo receiver è quello corrente, sposta l'agente all'interno della sottomatrice
            if (receiver == rank) {
                sub_matrix[destRow + destination.column_index] = sub_matrix[current_index];
                move[destRow + destination.column_index] = 0;
            } else {
                //Creazione di una struct con le informazioni sullo spostamento e la aggiunge all'array dei dati destinati al processo receiver
                move_agent var = {destRow, destination.column_index, sub_matrix[current_index]};
                data[receiver][num_elems_to_send_to[receiver]++] = var;
            }
            //Aggiornamento degli array per lo spostamento 
            move[current_index] = -1;
            sub_matrix[current_index] = EMPTY;
            used_empty_cells++;
        }
    }

    //Sincronizza i dati tra i processi
    sync(rank, size, num_elems_to_send_to, data, sub_matrix, move_agent_type);

    free(num_elems_to_send_to);
}

//Funzione che gestisce la sincronizzazione dei dati tra i vari processi
void sync(int rank, int size, int *num_elems_to_send_to, move_agent **data, char *sub_matrix, MPI_Datatype move_agent_type) {
    int empty_cell_used[size];
    MPI_Request requests1[size];
    MPI_Request requests2[size];
    
    //Array contenente gli agenti ricevuti da altri processi
    move_agent **moved_agents = (move_agent **)malloc(sizeof(move_agent *) * size);
    
    //Array contenente gli agenti da inviare
    move_agent **elements_to_send = (move_agent **)malloc(sizeof(move_agent *) * size);

    for (int i = 0; i < size; i++) {
        //Se il processo corrente è il processo in esecuzione, passa all'iterazione successiva
        if (i == rank) continue;

        //Invia in modo asincrono al processo i il numero di elementi che il processo corrente intende inviare
        MPI_Isend(&num_elems_to_send_to[i], 1, MPI_INT, i, 99, MPI_COMM_WORLD, &requests1[i]);
        
        //Riceve in modo asincrono dal processo i il numero di celle vuote utilizzate da quel processo
        MPI_Irecv(&empty_cell_used[i], 1, MPI_INT, i, 99, MPI_COMM_WORLD, &requests1[i]);
        
        //Si attende che tutte le operazioni non bloccanti siano completate 
        MPI_Wait(&requests1[i], NULL);

        elements_to_send[i] = data[i];

        //Puntatore per gli elementi da inviare
        moved_agents[i] = (move_agent *)malloc(empty_cell_used[i] * sizeof(move_agent));

        //Invia in modo asincrono gli agenti al processo i 
        MPI_Isend(elements_to_send[i], num_elems_to_send_to[i], move_agent_type, i, 100, MPI_COMM_WORLD, &requests2[i]);
        
        //Riceve in modo asincrono gli agenti dal processo i
        MPI_Irecv(moved_agents[i], empty_cell_used[i], move_agent_type, i, 100, MPI_COMM_WORLD, &requests2[i]);
        
        //Si attende che tutte le operazioni non bloccanti siano completate 
        MPI_Wait(&requests2[i], NULL);

        //Calcola l'indice di destinazione e aggiorna sub_matrix con l'agente ricevuto
        for (int k = 0; k < empty_cell_used[i]; k++) {
            int dest_index = moved_agents[i][k].dest_row + moved_agents[i][k].dest_column;
            sub_matrix[dest_index] = moved_agents[i][k].agent;
        }

        free(moved_agents[i]);
    }

    free(moved_agents);
    free(elements_to_send);
}

//Funzione che stampa le informazioni finali della computazione, come il numero di agenti e la percentuale di soddisfazione
void print_final_info(info_agents *agents_info, int unsatisfied_count, float satisfaction_percentage) {
    printf("- Total Agents: %d\n", agents_info->tot_agents);
    
    printf("-");
    PRINT_BLUE('X');
    printf("Agents: %d\n", agents_info->tot_x_agents);

    printf("-");
    PRINT_RED('O');
    printf("Agents: %d\n", agents_info->tot_o_agents);

    printf("- Satisfied Agents: %d\n", agents_info->tot_agents - unsatisfied_count);
    if (unsatisfied_count > 0) {
        printf("- Unsatisfied Agents: %d\n", unsatisfied_count);
    }
    printf("- Satisfaction Percentage: %.3f%%\n", satisfaction_percentage);
}

//Funzione che calcola il livello di soddisfazione totale ottenuto in seguito allo spostamento degli agenti
void final_satisfation(int rank, int size, char *matrix) {

    //Inizializza i membri della struct a 0
    info_agents agents_info = {0, 0, 0};

    //Array che contiene gli agenti insoddisfatti
    move_agent *not_satisfied_agents = malloc(ROWS * COLUMNS * sizeof(move_agent));
    
    //Contatore degli agenti insodisfatti
    int unsatisfied_count = 0;

    //Scorre ogni cella della matrice
    for (int i = 0; i < ROWS; i++) {
        for (int j = 0; j < COLUMNS; j++) {
            char agent_type = matrix[i * COLUMNS + j];

            //Verifica se la cella non è vuota
            if (agent_type != EMPTY) {
                agents_info.tot_agents++;

                //Se incontro l'agente X, incremento il contatore, lo stesso in presenza dell'agente O
                if (agent_type == 'X') agents_info.tot_x_agents++;
                if (agent_type == 'O') agents_info.tot_o_agents++;
                
                //Verifica se l'agente nella cella corrente è soddisfatto, in tal caso passa alla cella successiva
                if (is_satisfied(rank, size, ROWS, ROWS, i * COLUMNS, j, matrix)) {
                    continue;
                } else {
                    //Aggiunge l'agente all'array degli agenti insoddisfatti e incrementa il contatore
                    move_agent var = {i * COLUMNS, j, agent_type};
                    not_satisfied_agents[unsatisfied_count] = var;
                    unsatisfied_count++;
                }
            }
        }
    }

    //Calcola la percentuale di soddisfazione come il rapporto tra il numero totale di agenti soddisfatti e il numero totale di agenti, moltiplicato per 100
    float satisfaction_percentage = ((double)(agents_info.tot_agents - unsatisfied_count) / (double)agents_info.tot_agents) * 100;
    
    //Stampa delle informazionni finali
    print_final_info(&agents_info, unsatisfied_count, satisfaction_percentage);

    free(not_satisfied_agents);
}
