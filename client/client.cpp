#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <pthread.h>
#include <vector>
#include <algorithm>
#include <map>

#define BUFFER_LEN 512

struct tracker_args
{
    std::string t_addr1;
    int t_port1;
};

struct client_args
{
    std::string c_addr;
    int c_port;
};

struct rev_peer_args
{
    int client_listen_sock;
    std::vector<int>& vec_rev_peer_sock;   
};

struct file_info
{
    std::string file_hash;
    std::string file_size;
    int num_chunks;
    std::map<int, std::string> chunk_hash;
    std::vector<struct client_args> file_loc;
};

struct down_chunk_args
{
    std::string file_name;
    std::string peer_addr;
    int peer_port;
    int chunk_idx;
    std::string *chunk_hash;
    std::string *chunk_str;
};

struct send_chunk_args
{
    int peer_sock;
    std::map<std::string, std::map<int, std::string>> *open_chunked_files;
    std::map<std::string, std::vector<int>> *files_status;
};

std::string curr_user = "";

pthread_mutex_t rev_peer_mutex = PTHREAD_MUTEX_INITIALIZER;

void get_tracker_info(const char * file_name, std::string &t_addr1, int * t_port1, std::string &t_addr2, int * t_port2)
{
    int inpt_fd = open(file_name, O_RDONLY);
    if (inpt_fd == -1)
    {
        perror("Error!: ");
        return;
    }

    struct stat inpt_file_info;
    stat(file_name, &inpt_file_info);
    int cnt = inpt_file_info.st_size;
    char * str_buf = new char[cnt];
    int num_bytes;
    int o = 0;
    num_bytes = read(inpt_fd, str_buf, cnt);
    if (num_bytes < 0)
    {
        perror("Error!: ");
        return;
    }
    char * line;
    if (num_bytes > 0)
    {
        line = strtok(str_buf,"\n");
    }
    while (line != NULL)
    {
        switch(o)
        {
            case 0: t_addr1 = line; break;
            case 1: *t_port1 = atoi(line); break;
            case 2: t_addr2 = line; break;
            case 3: *t_port2 = atoi(line); break;
        }
        line = strtok(NULL, "\n");
        ++o;
    }

    delete[] str_buf;
    close(inpt_fd);
}

int connect_to_tracker(struct tracker_args * track_ar)
{
    struct sockaddr_in serv_addr;

    int cl_track_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (cl_track_sock < 0)
    {
        perror("Error!: ");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(track_ar->t_port1);
    inet_pton(AF_INET, track_ar->t_addr1.c_str(), &(serv_addr.sin_addr));

    if ((connect(cl_track_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) < 0)
    {
        perror("Error!: ");
        return -2;
    }

    return cl_track_sock;
}

int start_client_listen(struct client_args * clnt_ar)
{
    int client_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (client_listen_sock < 0)
    {
        perror("Error!: ");
        return -1;
    }

    int opt = 1;
    if (setsockopt(client_listen_sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("Error!: ");
        return -2;
    }

    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(clnt_ar->c_port);
    inet_pton(AF_INET, clnt_ar->c_addr.c_str(), &(client_addr.sin_addr));

    if (bind(client_listen_sock, (struct sockaddr *) &client_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("Error!: ");
        return -3;
    }

    if (listen(client_listen_sock, 4) < 0)
    {
        perror("Error!: ");
        return -4;
    }

    return client_listen_sock;
}

void * accept_peer_connect(void * args)
{
    struct rev_peer_args * rp_args = ((struct rev_peer_args *)args);
    int client_listen_sock = rp_args->client_listen_sock; 
    int rev_peer_sock = 0;
    struct sockaddr_in peer_addr;

    socklen_t addr_len;

    addr_len = sizeof(peer_addr);

    while(1)
    {
        rev_peer_sock = accept(client_listen_sock, (struct sockaddr *)&peer_addr, &addr_len);
        if (rev_peer_sock < 0)
        {
            perror("Error!: ");
            return NULL;
        }

        pthread_mutex_lock(&rev_peer_mutex);
        rp_args->vec_rev_peer_sock.push_back(rev_peer_sock);
        pthread_mutex_unlock(&rev_peer_mutex);    
    }
    
    return NULL;
}

int connect_to_peer(const std::string &p_addr, int p_port)
{
    struct sockaddr_in peer_addr;

    int peer_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (peer_sock < 0)
    {
        perror("Error!: ");
        return -1;
    }

    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(p_port);
    inet_pton(AF_INET, p_addr.c_str(), &(peer_addr.sin_addr));

    if ((connect(peer_sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr))) < 0)
    {
        perror("Error!: ");
        std::cout << "Here" << std::endl;
        return -2;
    }

    return peer_sock;
}

std::vector<std::string> get_args(const std::string &command)
{
    char * token;
    std::vector<std::string> comm_args;

    token = strtok(const_cast<char *>(command.c_str()), " ");

    while(token != NULL)
    {
        comm_args.push_back(token);
        token = strtok(NULL, " ");
    }

    return comm_args;
}

struct file_info read_tor_file(int cl_track_sock, std::vector<std::string> &comm_args)
{
    std::string args = "download_file "; 
    for (unsigned int i = 0; i < comm_args.size()-1; i++)
    {
        args += comm_args[i] + " ";
    }
    args += comm_args[comm_args.size()-1];
    const char * message = args.c_str(); 
    write(cl_track_sock, message, strlen(message));

    char buffer[BUFFER_LEN] = { 0 }; int n = 1;
    std::string tor_file = "";
    int tor_file_size = 0;

    n = read(cl_track_sock, buffer, BUFFER_LEN);
    if (n < 0)
    {
        perror("Error!: ");
        return {};
    }
    tor_file_size = atoi(buffer);
    memset(buffer, '\0', BUFFER_LEN);
    int bytes_read = 0;

    while (bytes_read < tor_file_size)
    {
        memset(buffer, '\0', BUFFER_LEN);
        n = read(cl_track_sock, buffer, BUFFER_LEN);
        if (n < 0)
        {
            perror("Error!: ");
            return {};
        }
        tor_file += buffer;
        bytes_read += n;
    }

    std::string file_hash;
    char * file_saveptr = NULL;
    char * line_saveptr = NULL; 
    char * line = strtok_r(const_cast<char *>(tor_file.c_str()), "\n", &file_saveptr);
    if (line != NULL)
    {
            file_hash = line;
            line = strtok_r(NULL, "\n", &file_saveptr);
    }

    std::string file_size;
    if (line != NULL)
    {
            file_size = line;
            line = strtok_r(NULL, "\n", &file_saveptr);
    }

    std::map<int, std::string> chunk_hash;
    int num_chunks = stoi(file_size)/BUFFER_LEN;

    for (int i = 0; i < num_chunks; i++)
    {
        line = strtok_r(NULL, "\n", &file_saveptr);
        if (line != NULL)
        {
            chunk_hash.insert(std::pair<int, std::string>(i, line));
        }
    }

    std::vector<struct client_args> file_loc;
    struct client_args file_client;

    char * token;
    while (line != NULL)
    {
        token = strtok_r(line, ":", &line_saveptr);
        file_client.c_addr = token;
        token = strtok_r(NULL, ":", &line_saveptr);
        file_client.c_port = atoi(token);
        file_loc.push_back(file_client);
        line = strtok_r(NULL, "\n", &file_saveptr);
    }

    struct file_info down_file_info = {
        file_hash,
        file_size,
        num_chunks,
        chunk_hash,
        file_loc
    };

    return down_file_info;
}

void * download_chunk(void * args)
{
    struct down_chunk_args * dchnk_args = (struct down_chunk_args *)args;
    int peer_sock = connect_to_peer(dchnk_args->peer_addr, dchnk_args->peer_port);
    if (peer_sock < 0)
    {
        perror("Error!: ");
        pthread_exit(NULL);
    }

    char buffer[BUFFER_LEN] = { 0 };

    int n = 0;

    std::string message = "get_chunk:" +  dchnk_args->file_name + ":" + std::to_string(dchnk_args->chunk_idx);   
    write(peer_sock, message.c_str(), message.size());

    n = read(peer_sock, buffer, BUFFER_LEN);
    if (n < 0)
    {
        perror("Error!: ");
        pthread_exit(NULL);
    }
    std::string chunk_inf = buffer; 
    dchnk_args->chunk_hash = new std::string(chunk_inf.substr(chunk_inf.find("chunk_hash: ")));

    n = read(peer_sock, buffer, BUFFER_LEN);
    if (n < 0)
    {
        perror("Error!: ");
        pthread_exit(NULL);
    }

    dchnk_args->chunk_str = new std::string(buffer);

    close(peer_sock);
    pthread_exit(NULL);
}

std::vector<int> get_chunk_info(int peer_sock, std::string file_name)
{ 
    std::string message = "get_chunk_info " + file_name;

    write(peer_sock, message.c_str(), message.size());    

    char buffer[BUFFER_LEN] = { 0 };    

    int n = read(peer_sock, buffer, BUFFER_LEN);
    if (n < 0)
    {
        perror("Error!: ");
        pthread_exit(NULL);
    }

    std::string peer_chunks_idx = buffer;
    peer_chunks_idx = peer_chunks_idx.substr(peer_chunks_idx.find(" "));

    char * token_saveptr = NULL;
    char * token = strtok_r(const_cast<char *>(peer_chunks_idx.c_str()), " ", &token_saveptr);

    std::vector<int> vec_peer_chnk;
    while (token != NULL)
    {
        vec_peer_chnk.push_back(atoi(token));
        token = strtok_r(NULL, " ", &token_saveptr);
    }

    return vec_peer_chnk;
}

bool chunk_comp(const std::pair<int, int> &a, const std::pair<int, int> &b)
{
    return a.second < b.second;
}

void download_file(int cl_track_sock, std::vector<std::string> &comm_args, std::map<std::string, std::vector<int>> &files_status)
{   
    std::string outp_file_name = comm_args[2] + "/" + comm_args[1];
    struct file_info down_file_info = read_tor_file(cl_track_sock, comm_args);
    std::vector<struct client_args> file_loc = down_file_info.file_loc;
    std::vector<std::string> chunk_down_file(down_file_info.num_chunks);
    std::vector<std::string> chunk_down_hash(down_file_info.num_chunks);
    std::map<int, std::vector<int>>avail_chunks;

    for (unsigned int i = 0; i < file_loc.size(); i++)
    {
        int p_sck = connect_to_peer(file_loc[i].c_addr, file_loc[i].c_port);
        if (p_sck < 0)
        {
            return;
        }
        avail_chunks.insert(std::pair<int, std::vector<int>>(i, get_chunk_info(p_sck, comm_args[1])));
        close(p_sck);
    }

    std::vector<std::pair<int, int>> chunk_order(down_file_info.num_chunks);
    std::vector<std::vector<struct client_args>> chunk_to_peer(down_file_info.num_chunks);
    for (unsigned int i = 0; i < file_loc.size(); i++)
    {
        for (unsigned int j = 0; j < avail_chunks[i].size(); j++)
        {
            chunk_order[avail_chunks[i][j]].first = avail_chunks[i][j];
            chunk_order[avail_chunks[i][j]].second += 1; 

            chunk_to_peer[avail_chunks[i][j]].push_back(file_loc[i]);
        }
    }

    std::sort(chunk_order.begin(), chunk_order.end(), chunk_comp);

    std::vector<pthread_t> vec_peer_thread(file_loc.size());
    struct down_chunk_args dchnk_args;
    dchnk_args.file_name = down_file_info.file_hash;
    for (int i = 0; i < down_file_info.num_chunks; i++)
    {
        dchnk_args.chunk_idx = chunk_order[i].first;
        dchnk_args.peer_addr = chunk_to_peer[i][0].c_addr;
        dchnk_args.peer_port = chunk_to_peer[i][0].c_port;
        dchnk_args.chunk_str = &chunk_down_file[i];
        dchnk_args.chunk_hash = &chunk_down_hash[i];
        files_status[down_file_info.file_hash].push_back(dchnk_args.chunk_idx);
        pthread_create(&vec_peer_thread[i], NULL, download_chunk, &dchnk_args);
    }

    for (int i = 0; i < down_file_info.num_chunks; i++)
    {
        pthread_join(vec_peer_thread[i], NULL);
    }

    int outp_fd = open(outp_file_name.c_str(), O_WRONLY|O_CREAT, S_IRWXU|S_IRGRP|S_IROTH);
    truncate(outp_file_name.c_str(), 0);

    for (int i = 0; i < down_file_info.num_chunks; i++)
    {
        write(outp_fd, chunk_down_file[i].c_str(), chunk_down_file[i].size());
    }

    close(outp_fd);
}

void * send_chunk(void * args)
{
    struct send_chunk_args * schnk_args = (struct send_chunk_args *)args;
    int peer_sock = schnk_args->peer_sock;
    int n = 0;

    char buffer[BUFFER_LEN] = { 0 };

    n = read(peer_sock, buffer, BUFFER_LEN);
    if (n < 0)
    {
        perror("Error!: ");
        return NULL;
    }
    
    std::string file_name;
    char * token_saveptr = NULL;
    char * token;

    if (strcmp(buffer, "get_chunk_info") == 0)
    {
        token = strtok_r(buffer, " ", &token_saveptr);
        token = strtok_r(NULL, " ", &token_saveptr);
        file_name = token;
        memset(buffer, '\0', BUFFER_LEN);
        std::string chunks_seq = "chunk_num_idx: ";
        std::vector<int> v;
        if ((*schnk_args->files_status).find(file_name) == (*schnk_args->files_status).end())
        {
            v = {};
        }
        else
        {
            v = (*schnk_args->files_status)[file_name];
        }
        for (unsigned int i = 0; i < v.size()-1; i++)
        {
            chunks_seq += (std::to_string(v[i]) + " ");
        }
        chunks_seq += std::to_string(v[v.size()-1]);
        write(peer_sock, chunks_seq.c_str(), chunks_seq.size());
    }
    
    token_saveptr = NULL;
    int chunk_idx = 0;
    if (strcmp(buffer, "get_chunk") == 0)
    {
        token = strtok_r(buffer, ":", &token_saveptr);
        token = strtok_r(NULL, ":", &token_saveptr);
        file_name = token;
        token = strtok_r(NULL, ":", &token_saveptr);
        chunk_idx = atoi(token);

        write(peer_sock, ((*schnk_args->open_chunked_files)[file_name][chunk_idx]).c_str(), BUFFER_LEN);
    }

    return NULL;
}

void upload_file(int cl_track_sock, std::vector<std::string> &comm_args)
{

    write(cl_track_sock, comm_args[0].c_str(), comm_args[0].size());

    struct stat up_file_info;
    stat(comm_args[0].c_str(), &up_file_info);
    int cnt = up_file_info.st_size;

    std::string file_size = std::to_string(cnt);
    write(cl_track_sock, file_size.c_str(), file_size.size());
}

void process_command(int cl_track_sock, const std::string &inpt, struct tracker_args track_ar, struct client_args clnt_ar, std::map<std::string, std::vector<int>> &files_status)
{
    std::vector<std::string> comm_args = get_args(inpt);

    if (inpt.find("download_file") != std::string::npos)
    {
        download_file(cl_track_sock, comm_args, files_status);
    }
    else if (inpt.find("upload_file") != std::string::npos)
    {
        upload_file(cl_track_sock, comm_args);
    }
    else if (inpt.find("create_group") != std::string::npos || inpt.find("join_group") != std::string::npos || inpt.find("leave_group") != std::string::npos || inpt.find("list_requests") != std::string::npos || inpt.find("logout") != std::string::npos)
    {
        if (curr_user != "")
        {
            std::string message = "";
            for (unsigned int i = 0; i < comm_args.size()-1; i++)
            {
                message += comm_args[i] + " ";
            }
            message += comm_args[comm_args.size()-1];
            message = message + " " + curr_user;
            write(cl_track_sock, message.c_str(), message.size());

            char buffer[BUFFER_LEN] = { 0 };
            int n = read(cl_track_sock, buffer, BUFFER_LEN);
            if (n < 0)
            {
                perror("Error!: ");
                return;
            }

            if (message.find("logout") != std::string::npos && strcmp(buffer, "User successfully logged out.") == 0)
            {
                curr_user = "";
            }

            std::cout << buffer << std::endl;

            memset(buffer, '\0', BUFFER_LEN);
        }
        else
        {
            std::cout << "Please login." << std::endl; 
        }
    }
    else
    {
        std::string message = "";
        for (unsigned int i = 0; i < comm_args.size()-1; i++)
        {
            message += comm_args[i] + " ";
        }
        message += comm_args[comm_args.size()-1];
        write(cl_track_sock, message.c_str(), message.size());

        char buffer[BUFFER_LEN] = { 0 };
        int n = read(cl_track_sock, buffer, BUFFER_LEN);
        if (n < 0)
        {
            perror("Error!: ");
            return;
        }

        if (message.find("login") != std::string::npos && strcmp(buffer, "User successfully logged in.") == 0)
        {
            curr_user = comm_args[1];
        }

        std::cout << buffer << std::endl;

        memset(buffer, '\0', BUFFER_LEN);
    }
}

int main(int argc, char const *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: ./client <IP>:<PORT> tracker_info.txt" << std::endl;
        return -127;
    }

    std::string t_addr1;
    int t_port1;    
    std::string t_addr2;
    int t_port2;

    get_tracker_info(argv[2], t_addr1, &t_port1, t_addr2, &t_port2);

    struct tracker_args track_ar = {
        t_addr1,
        t_port1
    };

    char * token = strtok(const_cast<char *>(argv[1]), ":");
    struct client_args clnt_ar;
    if (token != NULL)
    {
        clnt_ar.c_addr = token;
        token = strtok(NULL, ":");
        if (token != NULL)
        {
            clnt_ar.c_port = atoi(token);
        }
    }

    int cl_track_sock = connect_to_tracker(&track_ar);
    if (cl_track_sock < 0)
    {
        return cl_track_sock;
    }

    int client_listen_sock = start_client_listen(&clnt_ar);

    std::vector<int> vec_rev_peer_sock;
    struct rev_peer_args rp_args = {
        client_listen_sock,
        vec_rev_peer_sock
    }; 
    pthread_t rev_peer_thread;
    pthread_create(&rev_peer_thread, NULL, accept_peer_connect, &rp_args);

    std::map<std::string, std::vector<int>> files_status;
    std::map<std::string, std::map<int, std::string>> open_chunked_files;

    std::vector<pthread_t> send_peer_thread(vec_rev_peer_sock.size());
        
    for (unsigned int i = 0; i < vec_rev_peer_sock.size(); i++)
    {
        struct send_chunk_args sp_args = {
            vec_rev_peer_sock[i],
            &open_chunked_files,
            &files_status
        };

        pthread_create(&send_peer_thread[i], NULL, send_chunk, &sp_args);
    }

    std::string inpt_line;
    while(1)
    {
        std::cout << "Client> ";
        getline(std::cin, inpt_line);

        process_command(cl_track_sock, inpt_line, track_ar, clnt_ar, files_status);

    }
    
    close(cl_track_sock);
    return 0;
}
