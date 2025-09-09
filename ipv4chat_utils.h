#include <stdio.h>
#include <sys/types.h>


#define BUF_SIZE		1000
#define NICKNAME_SIZE	50
#define IP_STRING_SIZE	15
#define EXTRA_SIZE		30

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

struct thread_arg {
	int sockfd;
	struct sockaddr_in machine_addr;
	char *nickname;
};

// Получение порта из строки
error_t get_port_from_str(char*, unsigned short*);
error_t get_ip_from_str(char*, struct in_addr*);
error_t get_ifname_from_ip(struct in_addr, char*);

// Ввод строки с ограничением
error_t input_with_limit(char*, size_t);

// Функции потоков для чтения и отправки сообщений
void *read_messages(void *args);
void *write_messages(void *args);
