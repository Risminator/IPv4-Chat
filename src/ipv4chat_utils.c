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

#define BUF_SIZE		1000
#define NICKNAME_SIZE	50
#define IP_STRING_SIZE	15
#define EXTRA_SIZE		30

#define CHECK_RESULT(res, msg)			\
do {									\
	if (res < 0) {						\
		perror(msg);					\
		exit(EXIT_FAILURE);				\
	}									\
} while (0)

// Тип ошибок для собственных функций. Используется один подход:
// error_t func(), можно проверить возвращаемое значение;
// type func(..., error_t *e), если нужно возвращаемое значение, то сохранить в отдельной пермеенной
typedef enum {
	NO_ERROR,
	ERROR_INTERNAL,
	ERROR_ILLEGAL_ARGS,
	ERROR_IO,
	ERROR_NOT_FOUND
} error_t;

// Данные, отправляемые в качестве аргумента функции потока
struct thread_arg {
	int sockfd;
	struct sockaddr_in machine_addr;
	char *nickname;
};

error_t get_ip_from_str(char *str, struct in_addr *inp) {
	// Проверка длины строки
	if (strlen(str) > IP_STRING_SIZE) {
		fprintf(stderr, "ERROR: Wrong IP format\n");
		return ERROR_ILLEGAL_ARGS;
	}

	// Занесение значения в структуру IPv4-адреса
	int res = inet_aton(str, inp);
	if (!res) {
		fprintf(stderr, "ERROR: Wrong IP format\n");
		return ERROR_ILLEGAL_ARGS;
	}
	return NO_ERROR;
}

error_t get_port_from_str(char *str, unsigned short *port) {
	// Перевод входной строки в число
	char *endptr = NULL;
	long int input_port = strtol(str, &endptr, 10);
	if (*endptr != '\0') {
		fprintf(stderr, "ERROR: Failed to convert %s\n", str);
		return ERROR_ILLEGAL_ARGS;
	}
	// Проверка значения порта
	if (input_port >= 0 && input_port <= 65535) {
		*port = input_port;
		return NO_ERROR;
	} else {
		fprintf(stderr, "ERROR: Port should be between 0 and 65535\n");
		return ERROR_ILLEGAL_ARGS;
	}
}

// Ввод одной строки из stdin по лимиту (не включая \0). Если строка пустая или слишком длинная, то предлагается повторить ввод
error_t input_with_limit(char *str, size_t str_limit) {
	int is_correct_string = 0;
	while (!is_correct_string) {
		if (!fgets(str, str_limit+1, stdin)) {
			fprintf(stderr, "ERROR: fgets: failed to input string\n");
			return ERROR_IO;
		}
		size_t len = strlen(str);
		if (len == 0) {
			fprintf(stdout, "ERROR: Please enter a string with at least 1 character\n");
		}
		// fgets завершает строку символом переноса. Нужно её убрать
		else if (str[len-1] == '\n') {
			str[len-1] = '\0';
			is_correct_string = 1;
		}
		// Если его нет, то строка либо ровно достигла предела, либо превысила его
		else {
			int ch;
			// Если превысила его, то будет найден следующий символ. Тогда очищаем поток ввода и повторно запрашиваем ввод.
			if ((ch = getchar()) != EOF && ch != '\n') {
				fprintf(stdout, "ERROR: String length is over the limit (%ld). Please try again.\n", str_limit);
				while((ch = getchar()) != EOF && ch != '\n');
			}
			else is_correct_string = 1;
		}
	}

	return NO_ERROR;
}

void *read_messages(void *args) {
	char *DEBUG = getenv("TASKDEBUG");
	struct thread_arg *a = (struct thread_arg*) args;
	if (DEBUG) {
		fprintf(stderr, "DEBUG: read_messages args: ip: %s, port: %d, nickname: %s\n",
				inet_ntoa((&(a->machine_addr))->sin_addr), ntohs(((a->machine_addr)).sin_port), a->nickname);
	}

	// Создаём копию для получения актуальной информации об адресе отправителя
	struct sockaddr_in sender_addr = {0};
	sender_addr.sin_family = (a->machine_addr).sin_family;
	sender_addr.sin_addr = (a->machine_addr).sin_addr;
	sender_addr.sin_port = (a->machine_addr).sin_port;
	socklen_t addr_size = sizeof(sender_addr);

	char buf[BUF_SIZE+NICKNAME_SIZE+IP_STRING_SIZE+EXTRA_SIZE];
	while(1) {
		memset(buf,0,sizeof(buf));
		int res = recvfrom(a->sockfd, buf, sizeof(buf), 0, (struct sockaddr*)(&sender_addr), &addr_size);
		CHECK_RESULT(res, "recvfrom");
		fprintf(stdout, "[%s] %s", inet_ntoa(sender_addr.sin_addr), buf);
	}
}

void *write_messages(void *args) {
	int res;
	char *DEBUG = getenv("TASKDEBUG");
	struct thread_arg *a = (struct thread_arg*) args;
	if (DEBUG) {
		fprintf(stderr, "DEBUG: write_messages args: ip: %s, port: %d, nickname: %s\n",
				inet_ntoa((a->machine_addr).sin_addr), ntohs(((a->machine_addr)).sin_port), a->nickname);
	}

	struct sockaddr_in broadcast_sockaddr = {0};
	broadcast_sockaddr.sin_family = (a->machine_addr).sin_family;
	broadcast_sockaddr.sin_port = (a->machine_addr).sin_port;
	broadcast_sockaddr.sin_addr.s_addr = INADDR_BROADCAST;
	socklen_t addr_size = sizeof(broadcast_sockaddr);

	char buf[BUF_SIZE+1];
	char buf_to_send[BUF_SIZE+NICKNAME_SIZE+EXTRA_SIZE];
	while (1) {
		memset(buf,0,sizeof(buf));
		memset(buf_to_send,0,sizeof(buf_to_send));
		input_with_limit(buf, BUF_SIZE);
		sprintf(buf_to_send, "%s: %s\n", a->nickname, buf);
		if (DEBUG) {
			fprintf(stdout, "\033[1F\033[1F\033[2K\r\n");
			fprintf(stderr, "DEBUG: ip_to_send_to: %s, buf_to_send: %s", inet_ntoa(broadcast_sockaddr.sin_addr), buf_to_send);
		} else {
			fprintf(stdout, "\033[1F\033[2K\r");
		}
		res = sendto(a->sockfd, buf_to_send, strlen(buf_to_send), 0, (const struct sockaddr*)(&broadcast_sockaddr), addr_size);
		CHECK_RESULT(res, "sendto");
	}
}

error_t get_ifname_from_ip(struct in_addr inp, char* ifa_name) {
	int res;
	char *DEBUG = getenv("TASKDEBUG");
	struct ifaddrs *if_addrs, *ifa;
	
	res = getifaddrs(&if_addrs);
	CHECK_RESULT(res, "getifaddrs");

	// Пройтись по всем интерфейсам и найти наш адрес
	for (ifa = if_addrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (!(ifa->ifa_addr)) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

		if (((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr == inp.s_addr) {
			strncpy(ifa_name, ifa->ifa_name, IF_NAMESIZE);
			ifa_name[IF_NAMESIZE-1] = '\0';

			if (DEBUG) {
				fprintf(stderr, "DEBUG: ifa_name: %s\n", ifa_name);
			}
			freeifaddrs(if_addrs);
			return NO_ERROR;
		}	
	}

	return ERROR_NOT_FOUND;
}

error_t get_broadcast_from_machine_ip(struct in_addr inp, struct in_addr *broadcast_inp) {
	int res;
	char *DEBUG = getenv("TASKDEBUG");
	struct ifaddrs *if_addrs, *ifa;
	
	res = getifaddrs(&if_addrs);
	CHECK_RESULT(res, "getifaddrs");

	// Пройтись по всем интерфейсам и найти наш адрес
	for (ifa = if_addrs; ifa != NULL; ifa = ifa->ifa_next) {
		if (!(ifa->ifa_addr)) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;

        struct sockaddr_in *if_addr = (struct sockaddr_in *)ifa->ifa_addr;
        struct sockaddr_in *if_mask = (struct sockaddr_in *)ifa->ifa_netmask;

		if (if_addr->sin_addr.s_addr == inp.s_addr) {
			uint32_t ip = ntohl(if_addr->sin_addr.s_addr);
			uint32_t mask = ntohl(if_mask->sin_addr.s_addr);
			uint32_t broadcast_ip = (ip & mask) | (~mask);

			broadcast_inp->s_addr = htonl(broadcast_ip);
			if (DEBUG) {
				fprintf(stderr, "DEBUG: get_broadcast_from_machine_ip: %s\n", inet_ntoa(*broadcast_inp));
			}
			freeifaddrs(if_addrs);
			return NO_ERROR;
		}	
	}

	return ERROR_NOT_FOUND;
}