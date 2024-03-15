#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define MAX_CLIENTS 10
#define MAX_BUFFER_SIZE 256

typedef struct {
    char id;
    int type;
    int are_segment[MAX_CHUNKS];
} Client;


typedef struct {
    char filename[MAX_FILENAME];
    char hash[MAX_CHUNKS][HASH_SIZE + 1];
    int number_of_chunks;
} FileSegment;

typedef struct {
    Client clients_list[MAX_CLIENTS];
    FileSegment file;
    int num_clients;
} Swarm;

typedef struct {
    int rank;
    int number_of_wanted_files;
    int number_of_owned_files;
    FileSegment *wanted_files;
    FileSegment *owned_files;
} ThreadInfo;


int randomPeer(Swarm* swarm_info, int segment, int rank) {
    int* clients_with_segment = NULL;
    int num_clients_with_segment = 0;

    // Find clients that have the desired segment
    for (int i = 0; i < swarm_info->num_clients; i++) {
        if (swarm_info->clients_list[i].are_segment[segment]) {
            clients_with_segment = realloc(clients_with_segment, (num_clients_with_segment + 1) * sizeof(int));
            clients_with_segment[num_clients_with_segment] = swarm_info->clients_list[i].id;
            num_clients_with_segment++;
        }
    }

    // If no client has the segment, return -1
    if (num_clients_with_segment == 0) {
        free(clients_with_segment);
        return -1;
    }

    // Randomly select a client from those that have the segment
    srand((unsigned int)time(NULL));
    int random_index = rand() % num_clients_with_segment;
    int selected_client = clients_with_segment[random_index];

    free(clients_with_segment);

    return selected_client;
}

void sendUpdateToTracker(const char* message, const char* filename, const Client* clients_list) {
    MPI_Send(message, strlen(message) + 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
    MPI_Send(filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
    MPI_Send(clients_list, sizeof(Client), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
}

void receiveInfoFromTracker(Swarm* swarm_info) {
    MPI_Recv(swarm_info, sizeof(Swarm), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

void *download_thread_func(void *arg) {
    MPI_Status status;
    ThreadInfo *thread_info = (ThreadInfo *)arg;
    FileSegment *wanted_files = thread_info->wanted_files;
    FileSegment *owned_files = thread_info->owned_files;
    int total_downloaded_segments = 0;
    int rank = thread_info->rank;

    int i = 0;

    while(i < thread_info->number_of_wanted_files) {
        Swarm swarm_info;
        Client new_clients_list;
        char output_filename[30];
        int collected_segments = 0;
        int new_file_idx = 0;
        new_file_idx = thread_info->number_of_owned_files++ ;

        MPI_Send("REQUEST", 7, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(wanted_files[i].filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Recv(&swarm_info, sizeof(Swarm), MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, &status);

        snprintf(thread_info->owned_files[new_file_idx].filename, FILENAME_MAX, "%s", wanted_files[i].filename);
        thread_info->owned_files[new_file_idx].number_of_chunks = swarm_info.file.number_of_chunks;
       
        for (int j = 0; j < swarm_info.file.number_of_chunks; j++) {
            new_clients_list.are_segment[j] = 0;
        }

        new_clients_list.type = 0;

        while (collected_segments < swarm_info.file.number_of_chunks) {
            char send_buffer[256];
            char received_chunk[HASH_SIZE + 1];

            int peer_rank = randomPeer(&swarm_info, collected_segments, rank);
            if (peer_rank < 0) {
                printf("No peer found\n");
            }
            
            int len = snprintf(send_buffer, sizeof(send_buffer), "REQUEST %s %d", wanted_files[i].filename, collected_segments);

            if (len >= sizeof(send_buffer)) {
                exit(-1);
            }

            send_buffer[len] = '\0';

            MPI_Send(send_buffer, strlen(send_buffer) + 1, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD);
            
            MPI_Recv(received_chunk, HASH_SIZE, MPI_CHAR, peer_rank, 0, MPI_COMM_WORLD, &status);
            received_chunk[HASH_SIZE] = '\0';
            
            MPI_Send("ACK", 4, MPI_CHAR, peer_rank, 1, MPI_COMM_WORLD);

            for (size_t i = 0; i < HASH_SIZE; ++i) {
                owned_files[new_file_idx].hash[collected_segments][i] = received_chunk[i];
            }

            new_clients_list.are_segment[collected_segments++] = 1;
            total_downloaded_segments++;
            
            if (total_downloaded_segments % 10 == 0) {
                sendUpdateToTracker("UPDATE", wanted_files[i].filename, &new_clients_list);
                receiveInfoFromTracker(&swarm_info);
            }

        }

        if (snprintf(output_filename, sizeof(output_filename), "client%d_%s", rank, wanted_files[i].filename) >= sizeof(output_filename)) {
            exit(-1);
        }

        FILE *output_file = fopen(output_filename, "w");
        if (output_file == NULL) {
            exit(-1);
        }


        MPI_Send("FIN_FILE", 12, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(owned_files[new_file_idx].filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

        int j = 0;
        while(j < owned_files[new_file_idx].number_of_chunks) {
            fprintf(output_file, "%s\n", owned_files[new_file_idx].hash[j]);
            j++;
        }

        fclose(output_file);

        i++;
    }

    MPI_Send("FINISH", 7, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

    return NULL;
}

void sendFileSegment(ThreadInfo* thread_info, int sender_rank, const char* filename, int requested_segment) {
    FileSegment* owned_files = thread_info->owned_files;

    for (int j = 0; j < thread_info->number_of_owned_files; j++) {
        if (strcmp(owned_files[j].filename, filename) == 0) {
            MPI_Send(owned_files[j].hash[requested_segment], HASH_SIZE, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);

            char ack_message[4];
            MPI_Status status;
            MPI_Recv(ack_message, sizeof(ack_message), MPI_CHAR, sender_rank, 1, MPI_COMM_WORLD, &status);
            return;
        }
    }

}

void* upload_thread_func(void* arg) {
    ThreadInfo* thread_info = (ThreadInfo*)arg;
    int rank = thread_info->rank;

    MPI_Status status;
    char received_buffer[MAX_BUFFER_SIZE];

    do {
        memset(received_buffer, 0, sizeof(received_buffer));
        MPI_Recv(received_buffer, sizeof(received_buffer), MPI_CHAR, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);

        int sender_rank = status.MPI_SOURCE;

        if (strncmp(received_buffer, "REQUEST", 7) == 0) {
            char request_message[10];
            char filename[MAX_FILENAME];
            int requested_segment;
            sscanf(received_buffer, "%s %s %d", request_message, filename, &requested_segment);

            sendFileSegment(thread_info, sender_rank, filename, requested_segment);
        } else if (strncmp(received_buffer, "CLOSE", 5) == 0) {
            return NULL;
        }

    } while (1);

    return NULL;
}

int findFileIndex(const Swarm *swarm, int num_files, const char *filename) {
    for (int i = 0; i < num_files; ++i) {
        if (strcmp(swarm[i].file.filename, filename) == 0) {
            return i;
        }
    }
    return -1;
}

int findClientIndex(Swarm swarm[], int file_index, int id) {
    int i = 0;
    while (i < swarm[file_index].num_clients && swarm[file_index].clients_list[i].id != id) {
        i++;
    }
    return (i < swarm[file_index].num_clients) ? i : -1;
}


void handleRequest(const Swarm *swarm, int num_files, int sender_rank, const char *requested_filename) {
    int file_index = findFileIndex(swarm, num_files, requested_filename);

    if (file_index != -1) {
        MPI_Send(&swarm[file_index], sizeof(Swarm), MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);
    } 
}

void handleUpdate(Swarm *swarm, int num_files, int sender_rank) {
    char update_filename[MAX_FILENAME];
    MPI_Recv(update_filename, MAX_FILENAME, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    int update_file_index = findFileIndex(swarm, num_files, update_filename);

    if (update_file_index != -1) {
        Client update_clients_list;
        MPI_Recv(&update_clients_list, sizeof(Client), MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        int update_client_index = findClientIndex(swarm, update_file_index, sender_rank);

        if (update_client_index == -1) {
            swarm[update_file_index].clients_list[swarm[update_file_index].num_clients].id = sender_rank;
            memcpy(swarm[update_file_index].clients_list[swarm[update_file_index].num_clients].are_segment, 
                   update_clients_list.are_segment, 
                   sizeof(update_clients_list.are_segment));
            swarm[update_file_index].num_clients++;
        } else {
            memcpy(swarm[update_file_index].clients_list[update_client_index].are_segment, 
                   update_clients_list.are_segment, 
                   sizeof(update_clients_list.are_segment));
        }

        MPI_Send(&swarm[update_file_index], sizeof(Swarm), MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);
    }
}

void handleFinishFile(Swarm *swarm, int num_files, int sender_rank) {
    char finished_filename[MAX_FILENAME];
    MPI_Recv(finished_filename, MAX_FILENAME, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    int finished_file_index = findFileIndex(swarm, num_files, finished_filename);

    int finished_client_index = findClientIndex(swarm, finished_file_index, sender_rank);

    if (finished_client_index != -1) {
        swarm[finished_file_index].clients_list[finished_client_index].type = 1;
        printf("Client finished downloading \n");
    }
}

void handleFinishAllDownloads(int *finished, int numtasks) {
    (*finished)--;
    if (*finished <= 0) {
        for (int i = 1; i < numtasks; i++) {
            MPI_Send("CLOSE", 6, MPI_CHAR, i, 1, MPI_COMM_WORLD);
        }
    }
}

void tracker(int numtasks, int rank) {
    Swarm swarm[MAX_FILES];
    MPI_Status status;
    int finished = numtasks - 1;
    int num_files = 0;
    int stop = 0;

   while (1) {
        char filename[MAX_FILENAME];
        int hash_size = HASH_SIZE + 1;
        MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        int sender_rank = status.MPI_SOURCE;
        char hash[hash_size];
        int num_segments;

        if (filename[0] == '\0') {
            MPI_Send("ACK\n", 4, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD);
            if (--(finished) <= 0) {
                int i = 1;
                while(i < numtasks) {
                    MPI_Send("START\n", 6, MPI_CHAR, i, 0, MPI_COMM_WORLD);
                    i++;
                }
                break;
            }
        } else {
            int file_index = -1;
            MPI_Recv(&num_segments, 1, MPI_INT, sender_rank, 1, MPI_COMM_WORLD, &status);
            int i = 0;

            while(i < num_files) {
                if (strcmp(swarm[i].file.filename, filename) == 0) {
                    file_index = i;
                    break;
                }
                i++;
            }

            if (file_index == -1) {
                int newFileIndex = num_files;

                swarm[newFileIndex] = (Swarm) {
                    .file = {.number_of_chunks = num_segments},
                    .num_clients = 1,
                    .clients_list[0] = {.id = sender_rank, .type = 1}
                };

                strcpy(swarm[newFileIndex].file.filename, filename);

                int i = 0;
                while(i < num_segments){
                    MPI_Recv(hash, HASH_SIZE, MPI_CHAR, sender_rank, 2, MPI_COMM_WORLD, &status);
                    hash[HASH_SIZE] = '\0';
                    Swarm* currentFile = &swarm[num_files];
                    currentFile->clients_list[0].are_segment[i] = 1;
                    strncpy(currentFile->file.hash[i], (currentFile->clients_list[0].are_segment[i]) ? hash : "", HASH_SIZE);
                    i++;
                }

                num_files++;
            } else {
                int client_index = -1;
                int i = 0;

                while (i < swarm[file_index].num_clients && (client_index = (swarm[file_index].clients_list[i].id == sender_rank) ? i : -1) == -1) {
                    i++;
                }


                if (client_index == -1) {
                    int aux = swarm[file_index].num_clients++;
                    swarm[file_index].clients_list[aux] = (Client) {.id = sender_rank, .type = 1};
                    int i = 0;
                    while( i < num_segments) {
                        swarm[file_index].clients_list[aux].are_segment[i] = 1;
                        i++;
                    }
                }
            }
        }
    }
    
    stop = numtasks - 1;
    char message[MAX_FILENAME];

    while(1) { 
        MPI_Recv(message, MAX_FILENAME, MPI_CHAR, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
        int sender_rank = status.MPI_SOURCE;
        bool ok = false;

        if (strncmp(message, "REQUEST", 7) == 0) {
            MPI_Recv(message, MAX_FILENAME, MPI_CHAR, sender_rank, 0, MPI_COMM_WORLD, &status);
            handleRequest(swarm, num_files, sender_rank, message);
        } else if (strncmp(message, "UPDATE", 6) == 0) {
            handleUpdate(swarm, num_files, sender_rank);
        } else if (strncmp(message, "FIN_FILE", 8) == 0) {
            handleFinishFile(swarm, num_files, sender_rank);
        } else if (strncmp(message, "FINISH", 6) == 0) {
            handleFinishAllDownloads(&stop, numtasks);
        }

        if (stop <= 0) {
            break;
        }
    }
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    char filename[20];

    snprintf(filename, sizeof(filename), "in%d.txt", rank);

    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        exit(-1);
    }

    char line[100];
    if (fgets(line, sizeof(line), file) == NULL) {
        exit(-1);
    }

    int number_of_owned_files;
    sscanf(line, "%d", &number_of_owned_files);

    FileSegment *owned_files = calloc(number_of_owned_files, sizeof(FileSegment));
    
    int i = 0;
    while(i < number_of_owned_files) {
        
        if(fgets(line, sizeof(line), file) == NULL) {
            exit(-1);
        }

        char file_name[MAX_FILENAME];
        int num_segments;
        if(sscanf(line, "%255s %d", file_name, &num_segments) != 2) {
          exit(-1);
        }

        strncpy(owned_files[i].filename, file_name, sizeof(owned_files[i].filename) - 1);
        owned_files[i].number_of_chunks = num_segments;
        owned_files[i].filename[sizeof(owned_files[i].filename) - 1] = '\0';
        
        // Send the file name and number of segments to the tracker with specific tags
        MPI_Send(file_name, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);
        MPI_Send(&num_segments, 1, MPI_INT, TRACKER_RANK, 1, MPI_COMM_WORLD);

        // Send the hash of each segment to the tracker with specific tags
        int j = 0;
        while(j < num_segments) {
            if(fgets(line, sizeof(line), file) == NULL) {
                exit(-1);
            }
            if (strlen(line) >= HASH_SIZE) {
                 memcpy(owned_files[i].hash[j], line, HASH_SIZE);
            }
            else {
                exit(-1);
            }
            MPI_Send(&line, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 2, MPI_COMM_WORLD);
            j++;
        }
        i++;
    }

    // Signal end of file registration with specific tag
    MPI_Send("", 1, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD);

    // Wait for the tracker
    MPI_Recv(line, 4, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, NULL);
 
    MPI_Recv(line, 6, MPI_CHAR, TRACKER_RANK, 0, MPI_COMM_WORLD, NULL);
 
    // Read the number of files to download
    fgets(line, sizeof(line), file);
    int num_files_to_download;
    sscanf(line, "%d", &num_files_to_download);

    FileSegment *wanted_files = calloc(num_files_to_download, sizeof(FileSegment));

    if (wanted_files == NULL) {
        exit(-1); 
    }    
    owned_files = realloc(owned_files, (num_files_to_download + number_of_owned_files + 1) * sizeof(FileSegment));

    // Process each file to download
    int k = 0;
    while(k < num_files_to_download) {
        fgets(wanted_files[k].filename, MAX_FILENAME, file);
        wanted_files[k].filename[strcspn(wanted_files[k].filename, "\n")] = '\0';
        k++;
    }

    fclose(file);

    // Create thread info structure
    ThreadInfo thread_info = {
        .rank = rank,
        .number_of_wanted_files = num_files_to_download,
        .wanted_files = wanted_files,
        .number_of_owned_files = number_of_owned_files,
        .owned_files = owned_files
    };


    int r;
    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&thread_info);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&thread_info);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }


    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
  
}

int main(int argc, char *argv[]) {
    int numtasks, rank;
    int provided;

    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        printf("MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
