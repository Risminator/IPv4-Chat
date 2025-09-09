#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include "ipv4chat_utils.h"

#define CHECK_RESULT(res, msg)			\
do {									\
	if (res < 0) {						\
		perror(msg);					\
		exit(EXIT_FAILURE);				\
	}									\
} while (0)

static char *g_program_name = "IPv4-Chat";
static char *g_version = "1.0";
static char *g_author = "Дронов Вадим Юрьевич";
static char *g_email = "vjdronov@yandex.ru";
static char *g_usage = "Usage: %s --ip <machine_ip> --port <listen_port>\n\n";


// Вывод справочной информации
void print_help();
void print_info();

int main(int argc, char *argv[]) {
	// Добавление переменной окружения позволяет получать отладочные сообщения
	char *DEBUG = getenv("TASKDEBUG");
	int res;

	// адрес машины IPv4 и слушаемый порт
	struct in_addr inp = {0};
	unsigned short listen_port;
	
	// Обработка опций
	int c, option_index;
	unsigned short flag_ip = 0, flag_port = 0;
	while(1) {
		// Используемые длинные опции
		enum long_option_values {
			VAL_HELP,
			VAL_IP,
			VAL_PORT,
		};
		static struct option long_options[] = {
			{"help",	no_argument,		0,	VAL_HELP},
			{"ip",		required_argument,	0,	VAL_IP},
			{"port",	required_argument,	0,	VAL_PORT},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv, "vhi:p:", long_options, &option_index);
		if (c==-1) 
	 		break;
		switch(c) {
			// Вывод версии программы
			case 'v':
				print_info();
				return EXIT_SUCCESS;
			// Опции не хватило аргумента
			case ':':
				fprintf(stderr, "Option needs a value\n");
				__attribute__ ((fallthrough));
			// Неизвестаня опция
			case '?':
				fprintf(stderr, g_usage, argv[0]);
				print_help();
				return EXIT_FAILURE;
			// Вывод вспомогательного сообщения
			case 'h':
				fprintf(stdout, g_usage, argv[0]);
				print_help();
				return EXIT_SUCCESS;
			// --help
			case VAL_HELP:
				fprintf(stdout, g_usage, argv[0]);
				print_help();
				return EXIT_SUCCESS;
			// --ip machine_ip
			case VAL_IP:
				if (optarg) {
					if (DEBUG) { fprintf(stderr, "DEBUG: Got an option for ip: %s\n", optarg); }
					error_t e = get_ip_from_str(optarg, &inp);
					if (e != NO_ERROR) {
						fprintf(stderr, "Error getting IP from string %s\n", optarg);
						return EXIT_FAILURE;
					}
					flag_ip = 1;
				} else {
					fprintf(stderr, "Option needs a value\n");
					fprintf(stderr, g_usage, argv[0]);
					print_help();
					return EXIT_FAILURE;
				}
				break;
			// --port listen_port
			case VAL_PORT:
				if (optarg) {
					if (DEBUG) { fprintf(stderr, "DEBUG: Got an option for port: %s\n", optarg); }
					error_t e = get_port_from_str(optarg, &listen_port);
					if (e != NO_ERROR) {
						fprintf(stderr, "Error getting port from string %s\n", optarg);
						return EXIT_FAILURE;
					}
					flag_port = 1;
				} else {
					fprintf(stderr, "Option needs a value\n");
					fprintf(stderr, g_usage, argv[0]);
					print_help();
					return EXIT_FAILURE;
				}
				break;
			default:
				fprintf(stderr, "WARNING: Getopt couldn't resolve the option\n");
				break;
		}
	}

	if(DEBUG) {
		fprintf(stderr, "DEBUG: argc: %d, optind: %d, flag_ip: %d, flag_port: %d\n", argc, optind, flag_ip, flag_port);
	}
	
	// Обработка случая, когда аргументов больше/меньше, чем нужно
	if (argc != optind || flag_ip != 1 || flag_port != 1)
	{
		if (flag_ip != 1)	fprintf(stderr, "ERROR: IP not supplied\n");
		if (flag_port != 1)	fprintf(stderr, "ERROR: Port not supplied\n");

		fprintf(stderr, g_usage, argv[0]);
		print_help();
		return EXIT_FAILURE;
	}

	if (DEBUG) {
		fprintf(stderr, "DEBUG: IP = %s, Port = %d\n", inet_ntoa(inp), listen_port);
	}

	// Ввод имени пользователя
	char nickname[NICKNAME_SIZE+1];
	fprintf(stdout, "PLease enter your nickname (%d bytes max): ", NICKNAME_SIZE);
	if (input_with_limit(nickname, NICKNAME_SIZE) != NO_ERROR) {
		fprintf(stderr, "Error getting input string\n");
		return EXIT_FAILURE;
	}

	fprintf(stdout, "Welcome, %s!\n", nickname);


	struct sockaddr_in machine_addr = {0};

	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	CHECK_RESULT(sockfd, "socket");

	// Опция для разрешения широковещания
	res = setsockopt(sockfd, SOL_SOCKET, SO_BROADCAST, &(int){1}, sizeof(int));
	CHECK_RESULT(res, "setsockopt: broadcast");

	// Опция для возможности быстрого переиспользования порта
	res = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int));
	CHECK_RESULT(res, "setsockopt: reuseaddr");

	char ifa_name[IF_NAMESIZE];
	if (get_ifname_from_ip(inp, ifa_name) != NO_ERROR) {
		fprintf(stderr, "Error getting interface name from IP %s\n", inet_ntoa(inp));
		return EXIT_FAILURE;
	}

	// Опция для привязки к конкретному сегменту подсети (по интерфейсу)
	res = setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, ifa_name, IF_NAMESIZE);
	CHECK_RESULT(res, "setsockopt: reuseaddr");


	// Адрес устройства и слушаемый порт: слушаем конкретную сеть
	machine_addr.sin_family = AF_INET;
	machine_addr.sin_port = htons(listen_port);
	machine_addr.sin_addr.s_addr = INADDR_ANY;

	res = bind(sockfd, (struct sockaddr*)&machine_addr, sizeof(machine_addr));
	CHECK_RESULT(res, "bind");

	if (DEBUG) {
		fprintf(stderr,"DEBUG: Listening to %s\n", inet_ntoa(machine_addr.sin_addr));
	}


	struct thread_arg targs = {
		sockfd,
		machine_addr,
		nickname
	};

	pthread_t msg_receiver_tid, msg_sender_tid;
	// Поток для чтения сообщений
	if ((res = pthread_create(&msg_receiver_tid, NULL, read_messages, &targs)) != 0) {
		errno = res;
		perror("pthread_create: msg_receiver");
		return EXIT_FAILURE;
	};
	// Поток для отправки сообщений
	if ((res = pthread_create(&msg_sender_tid, NULL, write_messages, &targs)) != 0) {
		errno = res;
		perror("pthread_create: msg_sender");
		return EXIT_FAILURE;
	};
	
	pthread_join(msg_receiver_tid, NULL);
	pthread_join(msg_sender_tid, NULL);
	if (DEBUG) fprintf(stderr, "All threads finished\n");

	close(sockfd);
	return EXIT_SUCCESS;
}

void print_help() {
	fprintf(stderr, "Available options:\n");
	fprintf(stderr, "--ip <machine_ip>\t(Mandatory) This client's address.\n");
	fprintf(stderr, "--port <listen_port>\t(Mandatory) Port to listen to.\n");
	fprintf(stderr, "-v\t\t\tPrint program version.\n");
	fprintf(stderr, "-h (--help)\t\tPrint this help message.\n");	
}

void print_info() {
	fprintf(stderr, "Program %s. Version: %s\n", g_program_name, g_version);
	fprintf(stderr, "Author: %s\n", g_author);
	fprintf(stderr, "Contact e-mail: %s\n", g_email);
}