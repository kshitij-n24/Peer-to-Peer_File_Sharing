#include <iostream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <pthread.h>
#include <vector>
#include <map>
#include <utility>

#define BUFFER_LEN 512

struct tracker_args
{
    std::string t_addr1;
    int t_port1;
};


struct user_info
{
    std::string username;
    std::string password;
};

struct user_sock_info
{
    std::string uname;
    std::string u_ip;
    int u_port;
};

struct group_info
{
    std::string group_admin;
    std::vector<std::string> group_members;
    std::vector<std::string> file_shared;
    std::vector<std::string> pending_requests;
};

std::map<std::string, std::string> user_details;
std::map<std::string, bool> user_state;
std::map<int, struct user_sock_info> user_sock;
std::map<int, group_info> map_group_info;

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

int start_tracker_listen(struct tracker_args * track_ar)
{
    int sock_fd = 0, opt = 1;

    struct sockaddr_in serv_addr;

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0)
    {
        perror("Error!:  ");
        return -1;
    }

    if (setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)) < 0)
    {
        perror("Error!: ");
        return -2;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(track_ar->t_port1);
    inet_pton(AF_INET, track_ar->t_addr1.c_str(), &(serv_addr.sin_addr));

    if (bind(sock_fd, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0)
    {
        perror("Error!: ");
        return -3;
    }

    if (listen(sock_fd, 4) < 0)
    {
        perror("Error!: ");
        return -4;
    }

    std::cout << "Tracker listening on port: " << track_ar->t_port1 << std::endl;

    return sock_fd;
}

int accept_clients(int serv_sock)
{
    int client_sock = 0;
    struct sockaddr_in client_addr;
    socklen_t addr_len;

    addr_len = sizeof(client_addr);

    client_sock = accept(serv_sock, (struct sockaddr *)&client_addr, &addr_len);
    if (client_sock < 0)
    {
        perror("Error!: ");
        return -5;
    }

    std::cout << "Got request at: " << client_sock << std::endl;

    char ip_address[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr.sin_addr), ip_address, INET_ADDRSTRLEN);
    int client_port = ntohs(client_addr.sin_port);

    struct user_sock_info uinfo = {
        "",
        ip_address,
        client_port
    };
    user_sock[client_sock] = uinfo;

    return client_sock;
}

void send_tor_file(int client_sock, std::vector<std::string> &comm_args)
{
    std::string tor_file_name = comm_args[2].substr(0, comm_args[2].find("."));
    std::string file_name = "group_" + comm_args[1] + "/tor_" + tor_file_name;
    
    std::string dir_name = "group_"+comm_args[1];
    struct stat chck_dir;
    int ret_val = stat(dir_name.c_str(), &chck_dir);
    if (ret_val != 0) 
    {
        std::string message = "Could not find tor file";
        write(client_sock, message.c_str(), message.size());
    }

    struct stat tor_file_info;
    stat(file_name.c_str(), &tor_file_info);
    std::string tor_file_size = std::to_string(tor_file_info.st_size);
    
    int tor_fd = open(const_cast<char *>(file_name.c_str()), O_RDONLY);
    if (tor_fd == -1)
    {
        perror("Error!: ");
        std::string message = "Could not find tor file";
        write(client_sock, message.c_str(), message.size());
        return;
    }

    write(client_sock, tor_file_size.c_str(), tor_file_size.size());

    char buffer[BUFFER_LEN] = { 0 };

    int num_bytes = read(tor_fd, buffer, BUFFER_LEN);

    while (num_bytes > 0)
    {
        write(client_sock, buffer, num_bytes);
        num_bytes = read(tor_fd, buffer, BUFFER_LEN);
    }
    close(tor_fd);
}

void upload_tor_file(int client_sock, std::vector<std::string> &comm_args)
{
    std::string file_name = comm_args[0].substr(comm_args[0].find_last_of("/"));

    std::string tor_file_name = "tor_" + file_name.substr(0, file_name.find(".")); 
    std::string group_dir = "group_" + comm_args[1];
    std::string file_path = group_dir + tor_file_name;

    struct stat chck_dir;
    int ret_val = stat(group_dir.c_str(), &chck_dir);
    if (ret_val != 0) 
    {
        int dir_created = mkdir(group_dir.c_str(), S_IRWXU);
        if (dir_created != 0)
        {
            perror("Error!: ");
            return;
        }
    }

    int res = access(file_path.c_str(), F_OK);
    int tor_fd = open(file_path.c_str(), O_WRONLY|O_CREAT, S_IRWXU|S_IRGRP|S_IROTH);
    if (res == 0)
    {
        lseek(tor_fd, 0, SEEK_END);
    }

    char buffer[BUFFER_LEN] = { 0 };
    int n = 0;
    if (res != 0)
    {
        n = read(client_sock, buffer, BUFFER_LEN);
        if (n < 0)
        {
            perror("Error!: ");
            return;
        }
        
        std::string file_size = buffer;
        n = read(client_sock, buffer, BUFFER_LEN);
        if (n < 0)
        {
            perror("Error!: ");
            return;
        }
        std::string tor_info = file_name + "\n" + file_size + "\n";

        write(tor_fd, tor_info.c_str(), tor_info.size());
    }
    std::string client_ip = user_sock[client_sock].u_ip;
    int client_port = user_sock[client_sock].u_port;

    std::string new_loc = client_ip + std::to_string(client_port);
    write(tor_fd, new_loc.c_str(), new_loc.size());

    close(tor_fd);
}

int create_user(std::vector<std::string> &comm_args)
{
    if (user_details.find(comm_args[0]) != user_details.end())
    {
        return -1;
    }

    user_details[comm_args[0]] = comm_args[1];
/*    struct user_info crt_user_info = {
        comm_args[0],
        comm_args[1]
    };*/

    user_state.insert(std::pair<std::string, bool>(comm_args[0], false));
    return 1;
}

int login_user(std::vector<std::string> &comm_args)
{
    if (user_details.find(comm_args[0]) == user_details.end())
    {
        return -1;
    }

    if (user_details[comm_args[0]] != comm_args[1])
    {
        return -2;
    }
    else
    {
        /*struct user_info usr_login_info = {
            comm_args[0],
            comm_args[1]
        };*/
        user_state[comm_args[0]] = true;
        return 1;
    }
}

int create_group(std::vector<std::string> &comm_args)
{
    std::vector<std::string> group_members = {};
    std::vector<std::string> file_shared = {};
    std::vector<std::string> pending_requests = {};

    struct group_info new_grp_info = {
        comm_args[1],
        group_members,
        file_shared,
        pending_requests
    };

    if (map_group_info.find(stoi(comm_args[0])) != map_group_info.end())
    {
        return -1;
    }

    map_group_info.insert(std::pair<int, struct group_info>(stoi(comm_args[0]), new_grp_info));
    return 1;
}

int join_group(std::vector<std::string> &comm_args)
{
    if (map_group_info.find(stoi(comm_args[0])) == map_group_info.end())
    {
        return -1;
    }

    for (unsigned int i = 0; i < map_group_info[stoi(comm_args[0])].group_members.size(); i++)
    {
        if (map_group_info[stoi(comm_args[0])].group_members[i] == comm_args[1])
        {
            return -2;
        }
        if (map_group_info[stoi(comm_args[0])].pending_requests[i] == comm_args[1])
        {
            return -3;
        }
    }

    map_group_info[stoi(comm_args[0])].pending_requests.push_back(comm_args[1]);
    return 1;
}   

int leave_group(std::vector<std::string> &comm_args)
{
    if (map_group_info.find(stoi(comm_args[0])) == map_group_info.end())
    {
        return -1;
    }

    for (unsigned int i = 0; i < map_group_info[stoi(comm_args[0])].group_members.size(); i++)
    {
        if (map_group_info[stoi(comm_args[0])].group_members[i] == comm_args[1])
        {
            map_group_info[stoi(comm_args[0])].group_members.erase(map_group_info[stoi(comm_args[0])].group_members.begin() + i);
            return 1;    
        }
    }

    return -2;
}

std::string list_requests(std::vector<std::string> &comm_args)
{
    std::string response = "";
    if (map_group_info.find(stoi(comm_args[0])) == map_group_info.end())
    {
        response = "Group does not exist";
        return response;
    }

    if (map_group_info[stoi(comm_args[0])].group_admin != comm_args[1])
    {
        response = "Only admin allowed to see the list.";
        return response;
    }

    response = "Pending requests: ";
    for (unsigned int i = 0; i < map_group_info[stoi(comm_args[0])].pending_requests.size(); i++)
    {
        response += map_group_info[stoi(comm_args[0])].pending_requests[i];
    }

    return response;
}

std::string accept_request(std::vector<std::string> &comm_args)
{
    std::string response = "";
    if (map_group_info.find(stoi(comm_args[0])) == map_group_info.end())
    {
        response = "Group does not exist";
        return response;
    }

    for (unsigned int i = 0; i < map_group_info[stoi(comm_args[0])].pending_requests.size(); i++)
    {
        if (map_group_info[stoi(comm_args[0])].pending_requests[i] == comm_args[1])
        {
            map_group_info[stoi(comm_args[0])].pending_requests.erase(map_group_info[stoi(comm_args[0])].pending_requests.begin() + i);
            map_group_info[stoi(comm_args[0])].group_members.push_back(comm_args[1]);
            response = "Group joining request accepted successfully.";
            return response;
        }
    }
    response = "Request could not be accpeted.";
    return response;
}

std::string list_groups()
{
    std::string response = "";

    response = "Groups: ";
    for (std::map<int, struct group_info>::iterator it = map_group_info.begin(); it != map_group_info.end(); it++)
    {
        response += (std::to_string(it->first) + " "); 
    }

    return response;
}

std::string list_files(std::vector<std::string> &comm_args)
{
    std::string response = "";

    if (map_group_info.find(stoi(comm_args[0])) == map_group_info.end())
    {
        response = "Group does not exist";
        return response;
    }

    response = "Files shared: ";
    for (unsigned int i = 0; i < map_group_info[stoi(comm_args[0])].file_shared.size(); i++)
    {
        response += (map_group_info[stoi(comm_args[0])].file_shared[i] + "; ");
    }
    return response;
}

int logout_user(std::vector<std::string> &comm_args)
{
    if (user_details.find(comm_args[0]) == user_details.end())
    {
        return -1;
    }

    /*struct user_info usr_login_info = {
        comm_args[0],
        user_details[comm_args[0]]
    };*/
    user_state[comm_args[0]] = false;
    return 1;
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

void process_command(int client_sock, const std::string &comm)
{
    // char buffer[BUFFER_LEN] = { 0 };

    // int n = read(client_sock, buffer, BUFFER_LEN);
    // if (n < 0)
    // {
    //     perror("Error!: ");
    //     return;
    // }

    std::string inpt = comm;
    std::string message;
    std::vector<std::string> comm_args = get_args(inpt);
    comm_args.erase(comm_args.begin());

    if (inpt.find("create_user") != std::string::npos)
    {
        int res = create_user(comm_args);
        if (res >= 0)
        {
            message = "User successfully created.";
        }
        else
        {
            message = "User could not be created.";
        }
        write(client_sock, message.c_str(), message.size());

    }
    else if (inpt.find("login") != std::string::npos)
    {
        int res = login_user(comm_args);
        if (res >= 0)
        {
            message = "User successfully logged in.";
        }
        else
        {
            message = "User could not be logged in.";
        }
        user_sock[client_sock].uname = comm_args[0];

        write(client_sock, message.c_str(), message.size());
    }
    else if (inpt.find("create_group") != std::string::npos)
    {
        int res = create_group(comm_args);
        if (res >= 0)
        {
            message = "Group created successfully.";
        }
        else
        {
            message = "Group could not be created.";
        }
        write(client_sock, message.c_str(), message.size());
    }
    else if (inpt.find("join_group") != std::string::npos)
    {
        int res = join_group(comm_args);
        if (res >= 0)
        {
            message = "Group joining request sent successfully.";
        }
        else
        {
            message = "Group joining request could not be sent/already a member of the group.";
        }
        write(client_sock, message.c_str(), message.size());
    }
    else if (inpt.find("leave_group") != std::string::npos)
    {
        int res = leave_group(comm_args);
        if (res >= 0)
        {
            message = "Group left successfully successfully.";
        }
        else
        {
            message = "Group could not be left.";
        }
        write(client_sock, message.c_str(), message.size());
    }
    else if (inpt.find("list_requests") != std::string::npos)
    {
        message = list_requests(comm_args);
        if (message.size() <= 0)
        {
            message = "Could not fetch the requests";
        }
        write(client_sock, message.c_str(), message.size());
    }
    else if (inpt.find("accept_request") != std::string::npos)
    {
        message = accept_request(comm_args);
        if (message.size() <= 0)
        {
            message = "Could not get response.";
        }
        write(client_sock, message.c_str()  , message.size());
    }
    else if (inpt.find("list_groups") != std::string::npos)
    {
        message = list_groups();
        if (message.size() <= 0)
        {
            message = "Could not fetch the groups.";
        }
        write(client_sock, message.c_str(), message.size());
    }
    else if (inpt.find("list_files") != std::string::npos)
    {
        message = list_files(comm_args);
        if (message.size() <= 0)
        {
            message = "Could not fetch the files.";
        }
        write(client_sock, message.c_str(), message.size());   
    }
    else if (inpt.find("upload_file") != std::string::npos)
    {
        upload_tor_file(client_sock, comm_args);
    }
    else if (inpt.find("download_file") != std::string::npos)
    {
        send_tor_file(client_sock, comm_args);
    }
    else if (inpt.find("logout") != std::string::npos)
    {
        int res = logout_user(comm_args);
        if (res >= 0)
        {
            message = "User successfully logged out.";
        }
        else
        {
            message = "User could not be logged out.";
        }
        user_sock[client_sock].uname = "";
        write(client_sock, message.c_str(), message.size());
    }
    else if (inpt.find("show_downloads") != std::string::npos)
    {
        std::cout << "show_downloads" << std::endl;
    }
    else if (inpt.find("stop_share") != std::string::npos)
    {
        std::cout << "stop_share" << std::endl;
    }
    
}

void * read_client(void * args)
{
    int * parse_args = (int *)args; 
    int client_sock = *parse_args;
    char buffer[BUFFER_LEN] = { 0 };
    int n = 0;

    while(1)
    {
        memset(buffer, '\0', BUFFER_LEN);
        n = read(client_sock, buffer, BUFFER_LEN);
        if (n < 0)
        {
            perror("Error!: ");
            return NULL;
        }
        printf("%s\n", buffer);
        if (strcmp(buffer, "quit") == 0)
        {
            break;
        }
        process_command(client_sock, buffer);
    }
    return NULL;
}

int main(int argc, char const *argv[])
{
    if (argc < 3)
    {
        std::cerr << "Usage: ./tracker tracker_info.txt tracker_no" << std::endl;
        return -1;
    }

    std::string t_addr1;
    int t_port1;    
    std::string t_addr2;
    int t_port2;
    get_tracker_info(argv[1], t_addr1, &t_port1, t_addr2, &t_port2);

    struct tracker_args track_ar = {
        t_addr1,
        t_port1
    };

    int serv_sock = start_tracker_listen(&track_ar);

    std::vector<int> client_sock;

    while (1)
    {
        client_sock.push_back(accept_clients(serv_sock));
        int read_client_arg = client_sock[client_sock.size() - 1]; 
        pthread_t thread;
        int res = pthread_create(&thread, NULL, read_client, &read_client_arg);
        if (res != 0)
        {
            perror("Error!: ");
            return -7;
        }
    }

    for (unsigned int i = 0; i < client_sock.size(); i++)
    {
        close(client_sock[i]);
    }
    close(serv_sock);
    return 0;
}
