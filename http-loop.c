
/*

	Invocar URL em loop
	Ferramenta para gerar trafego do projeto internet-sim

	Licenca: GNU/GPL

*/

#include "http-loop.h"

// contantes
#define USER_AGENT "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_11_1) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/46.0.2490.86 Safari/537.36"

// variaveis globais
//------------------------------------------------------------------------@@@@@@@@@@@@@@@

// protocolo IP usado para requisicao
int ip_proto = AF_INET; // AF_INET=ipv4, AF_INET6=ipv6

// string de trabalho
char *buffer = NULL;
#define BUFFER_SIZE	4092

// modo debug? 0=silencio, 1=cabecalho, 2=corpo
int verbose = 0;

// numero de requisicoes por url, -1 = infinito
int count = -1;

// numero total de requisicoes, -1 = infinito
int total = -1;

// paralelizar (modo louco)
int paralelize = 0;

// fech - obter sub-links da pagina
int fecthlinks = 0;

// em background?
int do_daemon = 0;

// arquivo com urls
char *urlfile = NULL;

// componentes da URL
typedef struct http_request_s {
	int proto;						// protocolo solicitado
	char *hostname;					// nome do host
	int port;						// porta do servidor http
	char *path;						// arquivo
	char *querystring;				// query-string (?var=value)
	char *url;						// URL completa reconstituida
	char *header;					// Cabecalho da requisicao (reaproveitavel)
	int header_len;					// numero de bytes no cabecalho
	int sockid;						// Socket keep-alive
} http_request;

// variaveis de trabalho
int proto = HTTP_NORMAL;				// protocolo solicitado
char *hostname = NULL;					// nome do host
int port = 0;							// porta
char *path = NULL;						// arquivo
char *querystring = NULL;				// query-string (?var=value)
#define STR_OR_EMPTY(X)		(X ? X : "")

// lista de URLs a processar
#define GLOBAL_SIZE 1024				// maximo 1024 urls a executar
http_request **global_list = NULL;				// lista de ponteiros
int global_count = 0;					// numero de elementos na lista
int global_index = 0;					// elemento atual da lista sendo lido

// Funcoes
//------------------------------------------------------------------------@@@@@@@@@@@@@@@

// liberar memoria da struct
#define HTTP_REQUEST_FREE(X)		if(X){ sfree(X->url); sfree(X->hostname); sfree(X->path); sfree(X->querystring); sfree(X->header); }

// imprimir objeto http_request
void http_request_print(http_request *obj){

	printf("............. IP Proto: %d\n", obj->proto);
	printf("            HTTP PROTO: %s (%d)\n", obj->proto == HTTP_NORMAL ? "http" : "https", obj->proto);
	printf("              HOSTNAME: %s\n", STR_OR_EMPTY(obj->hostname));
	printf("                  PORT: %d\n", obj->port);
	printf("                  PATH: %s\n", STR_OR_EMPTY(obj->path));
	printf("           QueryString: %s\n", STR_OR_EMPTY(obj->querystring));
	printf("                   URL: %s\n", STR_OR_EMPTY(obj->url));
	printf("\n");

}


// testar se string esta no formato ipv4
// porta na mesma declaracao
int is_fqdn(char *str){
	register int i;
	int test = 0;
	int len = strlen(str);
	int bcount = 0, dcount = 0;
	int have_dot = 0;

	// impossivel ter um nome menor que 3 bytes: x.y
	if(len < 3) return 8;
	// impossivel ter um mac maior que 256 bytes
	if(len > 254) return 9;

	// analise de caracteres
	for(i=0;i<len;i++){
		char ch = str[i];
		char bf = 0;

		// caracter anterior
		if(i) bf = str[i-1];

		// converter para minusculo
		ch = tolower(ch);

		// tem ponto?
		if(ch=='.') have_dot++;

		// caracter proibido
		if(ch != '.' && ch != '-' && !isalnum(ch)) return 7;

		// nao pode comecar com ponto ou traco
		if(!i && (ch=='.'||ch=='-')) return 6;

		// nao pode terminar com traco
		// (pode terminar com ponto, FQDN de dns termina com ponto)
		if(i+1==len && ch=='-') return 5;

		// nao pode ter ponto ou traco duplo
		if( (ch=='.' || ch=='-') && bf == ch ) return 4;

	}

	// sem ponto, nome simples nao pode ser FQDN
	if(!have_dot) return 3;

	// passou por todas as verificacoes de erros
	return 0;
}

// liberar memoria
#define XFREE(PTR)		if(PTR != NULL) free(PTR); PTR = NULL
void sfree(void *ptr){
	if(ptr != NULL) free(ptr);
	ptr = NULL;
}

// ler linha
char *file_readline(FILE *file) {
    if (file == NULL) return NULL;
    int maximumLineLength = 128;
    char *linebuffer = (char *)calloc(maximumLineLength, sizeof(char));
    if (linebuffer == NULL) return NULL;

    char ch = getc(file);
    int count = 0;
    while ((ch != '\n') && (ch != EOF)) {
        if (count == maximumLineLength) {
            maximumLineLength += 128;
            linebuffer = realloc(linebuffer, maximumLineLength);
            if (linebuffer == NULL) return NULL;
        }
        linebuffer[count] = ch;
        count++;
        ch = getc(file);
    }
    linebuffer[count] = '\0';
    return linebuffer;
}

// mover caracteres para esquerda (nao afeta ponteiro alocado)
void str_move_left(char *str, int n){
	int len;
	register int i;

	// parametros invalidos
	if(!n || !str || !str[0]) return;

	// tamanho
	len = strlen(str);

	// moveu o suficiente para apagar tudo
	if(len <= n ){ bzero(str, len); return; }

	// percorrer do primeiro ao ultimo
	for(i=0;i<len;i++){

		// caracter la da frente
		int k = i + n;

		// querendo pegar fora da string
		if(k >= len){
			// acabou
			str[i] = 0;
			return;
		}
		str[i] = str[k];
	}
	return;
}

// fazer trim (ltrim e rtrim) sem perder o ponteiro
// caracteres que sofrem trim: \n, espaco, \t, \r
#define str_is_trimchar(chr)	(chr=='\r'||chr=='\t'||chr==' '||chr=='\n')
void str_ptrim(char *str){
	register int len, i;

	// nao da pra processar isso
	if(!str || !str[0]) return;

	// trim left
	while(str_is_trimchar(str[0])) str_move_left(str, 1);

	// atualizar tamanho
	len = strlen(str);
	if(!len) return;

	// apagar tudo de traz pra frente
	for(i=len-1;i>=0;i--) if(str_is_trimchar(str[i])) str[i] = 0;
}

// ler url de entrada
http_request *read_url(char *input){
	http_request *ret = NULL;

	char *cursor = input;
	int len = 0;
	char *p=NULL, *q=NULL;

	len = strlen(input);
	if(!len) return ret;

	// porta zerada
	port = 0;


	// liberar alocacoes anteriores das variaveis globais
	XFREE(hostname);
	XFREE(path);
	XFREE(querystring);

	// ler inicio
	proto = HTTP_NORMAL;
	if(strncmp(cursor, "http://", 7)==0){

		// HTTP
		cursor += 7;
		port = 80;

	}else if(strncmp(cursor, "https://", 8)==0){

		// HTTPS
		proto = HTTP_SSL;
		port = 443;
		cursor += 8;

	}else if(strncmp(cursor, "ftp://", 6)==0){

		// nao lemos FTP
		return ret;

	}

	// bug: temos que ignorar todos os bytes que nao sejam de a-z 0-9 . -
	//      para iniciar a leitura do hostname
	// FALTA
	//printf("*** CURSOR.: %s\n", cursor);

	// tudo ate o proximo / e' o nome do host
	p = strchr(cursor, '/');
	if(p){
		int plen;

		// ler hostname
		hostname = calloc(p-cursor+1, sizeof(char) );
		strncpy(hostname, cursor, p-cursor);

		// ler caminho do arquivo e querystring
		plen = strlen(p);
		if(plen) path = strdup(p);

		// procurar query-string
		if(path){
			q = strchr(path, '?');
			if(q){
				// temos QS

				// finalizar path
				*q = 0;

				// ler querystring
				if(++q){
					int qlen = strlen(q);
					if(qlen) querystring = strdup(q);
				}

				// nao podemos ler caminho de target html interpretado pelo navegdor: #
				q = strchr(querystring, '#');
				if(q) *q = 0;

			}
		}

	}else{

		// sem barra, hostname e' o resto
		int x = strlen(cursor);
		hostname = calloc(x, sizeof(char) );
		strncpy(hostname, cursor, x);

		// padrao /
		path = strdup("/");

		// sem querystring
		querystring = NULL;

	}

	// Separar a porta do hostname
	p = strchr(hostname, ':');
	if(p){
		char *e;

		int pi = 0;
		char sport[6];
		bzero(sport, 6);

		// fechar string na posicao do :
		*p = 0;
		p++;

		// ler itens numericos
		while(isdigit(*p) && pi < 6){
			sport[pi] = *p;
			p++;
			pi++;
		}
		port = atoi(sport);

		/*
		// converter para numero decimal a moda antiga, atoi() deu merda
		if(pi && sport[0]){
			int dec = 0;
			int mu = 1;
			while(pi--){
				dec += (sport[pi]-48) * mu;
				mu *= 10;
			}
			printf("\n\nSPORT: %s DEC: %d\n\n", sport, dec);
			port = (int)dec;
		}
		*/
	}

	// validar hostname
	int fqdn_test = is_fqdn(hostname);
	if(fqdn_test){
		if(verbose) printf("*> FQDN INVALIDO: [%s] erro (%d)\n", hostname, fqdn_test);
		return ret;
	}

	// montar struct corretamente
	ret = calloc(1, sizeof(http_request));

	// remontar URL
	ret->url = calloc( 8 + strlen(hostname) + 1 + strlen(path) + 1 + (querystring ? strlen(querystring) : 1), sizeof(char));
	sprintf(ret->url, "%s://%s%s%s%s",
		proto == HTTP_NORMAL ? "http" : "https",
		hostname,
		path,
		querystring ? "?" : "",
		STR_OR_EMPTY(querystring)
	);

	// porta padrao
	if(!port) port = HTTP_NORMAL ? 80 : 443;

	// duplicar strings para usar na struct
	ret->proto = proto;
	ret->hostname = strdup(hostname);
	ret->port = port;
	ret->path = strdup(path);
	if(querystring) ret->querystring = strdup(querystring);

	return ret;
}

// liberar ram
void app_free(){
	register int i;

	// liberar memoria
	for(i=0; i < global_count; i++){ HTTP_REQUEST_FREE(global_list[i]); XFREE(global_list[i]); }
	XFREE(global_list);
	XFREE(buffer);
	XFREE(urlfile);
	XFREE(hostname);
	XFREE(path);
	XFREE(querystring);
}

// Funcao de ajuda
void help(){

	printf("\n");
	printf("Use: http-loop [-4] [-6] [-c count] [-f file-list] (url)\n");
	printf("Opcoes\n");
	printf("\t-p         : paralelize (um processo em loop para cada url)\n");
	printf("\t-4         : usar IPv4 (padrao)\n");
	printf("\t-6         : usar IPv6\n");
	printf("\t-d         : rodar em background\n");
	printf("\t-x         : fazer requisicoes em todos os links simples da pagina\n");
	printf("\t-c COUNT   : numero de requisicoes por url\n");
	printf("\t-t TOTAL   : numero maximo de requisicoes a executar\n");
	printf("\t-f FILE    : arquivo com lista de urls (Alexa?)\n");
	printf("\n");
	exit(1);
}

// Criar socket com conexao TCP com suporte IPv4 / IPv6
#define SOCK_CLOSE(S)	if(S>0){ shutdown(S, SHUT_RDWR); close(S); S=0; }
int socket_connect(int ip_proto, char *host, int port){
	struct hostent *hp;
	struct sockaddr_in addr;
	struct sockaddr_in addr4;
	struct sockaddr_in6 addr6;
	int on = 1, sock;

   struct timeval timeout;      
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

	// porta padrao: 80
	if(!port) port = 80;

	// IPv4 ?
	if(ip_proto == AF_INET){

		// Resolver HOST
		if((hp = gethostbyname(host)) == NULL) return -1;

		bcopy(hp->h_addr, &addr4.sin_addr, hp->h_length);

		addr4.sin_port = htons(port);
		addr4.sin_family = AF_INET;

		sock = socket(AF_INET, SOCK_STREAM, 0);
		if(sock < 0) return -3;

		// erro ao criar socket
		if(sock == -1) return -2;

		// setar timeout
		if (setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) error("setsockopt failed\n");
		if (setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) error("setsockopt failed\n");

		// iniciar 3-way handshake		
		if(connect(sock, (struct sockaddr *)&addr4, sizeof(struct sockaddr_in)) == -1) return -3;

		if(verbose) printf("socket_connect -> IPv4\n");

		// socket criado e pronto para troca de dados
		return sock;
	}


	// IPv6 ?
	if(ip_proto == AF_INET6){

		// Resolver HOST
		if((hp = gethostbyname2(host, AF_INET6)) == NULL) return -1;

		addr6.sin6_addr = in6addr_any;
		bcopy(hp->h_addr, &addr6.sin6_addr, hp->h_length);


		addr6.sin6_port = htons(port);
		addr6.sin6_family = AF_INET6;

		addr6.sin6_flowinfo = 0;
		addr6.sin6_scope_id = 0;

		sock = socket(AF_INET6, SOCK_STREAM, 0);

		// erro ao criar socket
		if(sock == -1){
			printf(" socket_connect -> error %d / -2\n", sock);
			return -2;
		}

		// setar timeout
		if (setsockopt (sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0) error("setsockopt failed\n");
		if (setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) error("setsockopt failed\n");

		// iniciar 3-way handshake		
		if(connect(sock, (struct sockaddr *) &addr6, sizeof(addr6)) < 0){
			printf(" socket_connect -> error %d / -3\n", sock);
			return -3;
		}

		if(verbose) printf("socket_connect -> IPv6\n");

		// socket criado e pronto para troca de dados
		return sock;

	}

	// Protocolo IP desconhecido (qual versao?)
	return -10;
}

// fazer requisicao HTTP
int http_client(http_request *obj){
	int writed = 0;
	int readed = 0;
	int total_readed = 0;
	int header_done = 0;
	int buffer_size = BUFFER_SIZE;

	// conteudo HTML
	char *html;

	// tipo de conteudo
	char *content_type;


	if(verbose) printf("http_client() start to get: %s\n", obj->url);

	// montar cabecalho
	if(!obj->header){

		// calcular tamanho
		int hlen = 0;

		// GET + path + HTTP/1.1 + \r\n
		hlen += 4 + strlen(obj->path) + (obj->querystring ? strlen(obj->querystring) : 0) + 9 + 2;
		// User-Agent: + user-agent + \r\n
		hlen += 12 + strlen(USER_AGENT) + 2;
		// Accept: */* + \r\n
		hlen += 11 + 2;
		// Host: + hostname + \r\n
		hlen += 6 + strlen(obj->hostname) + 2;
		// Connection: Keep-Alive
		hlen += 22 + 2;

		// MORE SPACE
		hlen += 20;

		obj->header = calloc(hlen, sizeof(char));

		sprintf(obj->header, "GET %s%s%s HTTP/1.1%sUser-Agent: %s%sAccept: */*%sHost: %s%sConnection: %s%s%s",
			obj->path, (obj->querystring ? "?" : ""), (obj->querystring ? obj->querystring : ""), HEADER_CR_NL,
			USER_AGENT, HEADER_CR_NL,
			HEADER_CR_NL,
			obj->hostname, HEADER_CR_NL,
			"Close", HEADER_CR_NL,
			HEADER_END
		);
		obj->header_len = strlen(obj->header);

	}

	//if(verbose) printf("\theader:\n%s\n", obj->header);

	// Conectar
	obj->sockid = socket_connect(ip_proto, obj->hostname, obj->port);
	if(obj->sockid < 1){
		if(verbose) printf("http_client() x> connect error\n");
		return 1;
	}

	if(verbose) printf("http_client() connect ok sockid=%d\n", obj->sockid);

	// Conectou!
	// Fazer requisicao HTTP
	writed = write(obj->sockid, obj->header, obj->header_len);
	if(writed != obj->header_len){ SOCK_CLOSE(obj->sockid); return 2; }

	if(verbose) printf("http_client() hequest sent, %d bytes\n", obj->header_len);

	if(verbose) printf("http_client() wait data...\n", obj->header_len);
	int readlen = 0;
	while(readlen = read(obj->sockid, buffer, buffer_size - 1) > 0){
		int rlen = strlen(buffer);

		ioctl(obj->sockid, FIONREAD, &readed);
		total_readed += readed;


		// nada mais a receber
		// if(!readed) break;
		if(!rlen) break;

		if(verbose > 1) printf("%s\n", buffer);
		if(verbose) printf("http_client() - input bytes: %d\n", rlen);

		// limpar buffer
		bzero(buffer, buffer_size);			

	}

	if(verbose) printf("http_client() DATA END, total=%d bytes\n", total_readed);

	// Desconectar
	SOCK_CLOSE(obj->sockid);

	return 0;
}

// processar lista
#define TEST_TOTAL		(total == -1 || total > 0)
#define TEST_COUNT		(count == -1 || count > 0)
#define TEST_COUNTERS	TEST_TOTAL && TEST_COUNT
#define DEC_TOTAL		if(total > 0) total--
#define DEC_COUNT		if(count > 0) count--
#define DEC_COUNTERS	DEC_TOTAL; DEC_COUNT
void exec_queries(){

	register int i;

	// processar
	//
	while( TEST_COUNTERS  ){

		for(i=0; i < global_count && TEST_COUNTERS; i++){

			if(verbose) printf("TOTAL: %d COUNT: %d IDX: %d URL: %s\n", total, count, i, global_list[i]->url);
			// http_request_print(global_list[i]);
			http_client(global_list[i]);

			DEC_COUNTERS;
		}
	}
	if(verbose) printf("exec_queires() - end\n");
	return;
}

// controle de pid
pid_t pid;
int mypid;

// PRINCIPAL
int main(int argc, char **argv){
	char ch;
	int argr = 0;
	register int i, j;
	http_request *_http_request;

	// alocar list
	global_list = calloc(GLOBAL_SIZE, sizeof(char*));

	// alocar espaco de trabalho no buffer
	buffer = calloc(BUFFER_SIZE, sizeof(char));

	// Proccessar parametros
	while ((ch = getopt(argc, argv, "c:f:t:dphv46x")) != EOF) {
		switch(ch) {

			case 'p':
				paralelize = 1;
				argr++;
				continue;

			case 'd':
				do_daemon = 1;
				argr++;
				continue;

			case 'c':
				count = atoi(optarg);
				if(count==0) count = -1;
				argr+=2;
				continue;

			case 't':
				total = atoi(optarg);
				if(total==0) total = -1;
				argr+=2;
				continue;

			case 'f':
				urlfile = strdup(optarg);
				argr+=2;
				continue;

			case '4':
				ip_proto = AF_INET;
				argr++;
				continue;

			case '6':
				ip_proto = AF_INET6;
				argr++;
				continue;

			case 'v':
				verbose++;
				argr++;
				continue;

			case 'x':
				fecthlinks=1;
				argr++;
				continue;

			case 'h':
			case '?':
				help();
				break;
		}
	}


	if(verbose){
		printf("ARGC: %d ARGR: %d\n", argc, argr);
		printf("VERBOSE...: %d\n", verbose);
		printf("COUNT.....: %d\n", count);
		printf("IP AF.....: %d\n", ip_proto);
	}

	// Ler urls nos parametros
	for(i = argr+1; i < argc && global_count < GLOBAL_SIZE; i++){
		_http_request = read_url(argv[i]);

		// url aceita
		if(_http_request) global_list[global_count++] = _http_request; else continue;

		if(verbose) printf("URL.......: [%c] %s\n", _http_request ? '-' : '+', argv[i]);

	}

	// Lers urls do arquivo
	if(urlfile){
		if(verbose) printf("READFILE..: %s\n", urlfile);
		FILE *file;
		file = fopen(urlfile, "r");
		if(file){
			while (!feof(file)) {
			    char *line = file_readline(file);
			    if(!line) break;

			    // remover espacos
			    str_ptrim(line);
			    if(verbose) printf("FILE [%s] ->LINE: %s\n", urlfile, line);

				// url aceita
				_http_request = read_url(line);
				if(_http_request) global_list[global_count++] = _http_request;
				
				// debug
				// if(_http_request && verbose) printf("URL.......: [%c] %s\n", '+', line);

				// liberar alocacao
				sfree(line);

			}
			fclose(file);			
		}
	}
	if(verbose) printf("ITENS.....: %d\n", global_count);
	
	// tem algo pra fazer?
	if(!global_count){
		if(verbose){
			printf("Sem urls para processar\n");
			help();
		}
		exit(1);
	}

	// aumentar limites
	struct rlimit limit;
	limit.rlim_cur = 655350;
	limit.rlim_max = 655350;
	if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
		if(verbose)
			printf("setrlimit() failed with errno=%d\n", errno);
		//-
		return 8;
	}

	// Get max number of files.
	if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
		if(verbose)
			printf("getrlimit() failed with errno=%d\n", errno);
		//-
		return 9;
	}

	// Descer o cacete!
	if(do_daemon){

		// FORK
		// mover para background
		if ((pid = fork()) < 0){
			perror("fork");
			exit(1);
		}
		if (pid == 0){

			// processo filho criado com sucesso
			exec_queries();

			/*
			if(paralelize){
				// criar forks

			}else{
				// 

			}
			*/

		}else{

			// copia criada, finalizar processo pai
			// e deixar o zumbi rodar
			exit(0);

		}



	}else{


		// foreground
		exec_queries();

	}


	// liberar ram
	app_free();
	if(verbose) printf("END!\n");

	return 0;
}

















