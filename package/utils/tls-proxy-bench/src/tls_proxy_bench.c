// SPDX-License-Identifier: GPL-2.0-only
/*
 * Verified streaming benchmark for PC -> TLS proxy -> PC connections.
 * Build: cc -O2 -pthread tls_proxy_bench.c -o tls-proxy-bench -lssl -lcrypto
 */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#define HEADER_SIZE 64
#define FOOTER_SIZE 32
#define RX 1
#define TX 2
#define MAX_CONNECTIONS 256
#define MAX_CHUNK (1024 * 1024)

struct address {
	char *host;
	char *port;
};

struct options {
	struct address proxy, backend;
	const char *server_name, *ca, *cipher;
	uint64_t bytes, seed;
	unsigned int chunk, connections, runs, warmups, timeout;
	int direction, version, cpu;
	bool insecure;
};

struct endpoint {
	int fd;
	SSL *ssl;
};

struct flow {
	struct endpoint *source, *sink;
	EVP_CIPHER_CTX *generate, *reference;
	unsigned char *out, *in, *expected;
	unsigned char footer[FOOTER_SIZE];
	size_t out_len, out_off, footer_out, footer_in;
	uint64_t total, sent, received, finished_ns;
	short write_want, read_want;
	const char *name;
};

struct trial {
	const struct options *opts;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	atomic_bool aborted;
	unsigned int ready;
	bool go;
};

struct connection {
	struct trial *trial;
	struct endpoint tls, plain;
	struct flow rx, tx;
	unsigned int id;
	pthread_t thread;
	char error[512];
};

static volatile sig_atomic_t interrupted;

static void on_signal(int sig)
{
	(void)sig;
	interrupted = 1;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts)) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static double cpu_seconds(void)
{
	struct rusage ru;

	if (getrusage(RUSAGE_SELF, &ru)) {
		perror("getrusage");
		exit(EXIT_FAILURE);
	}
	return ru.ru_utime.tv_sec + ru.ru_stime.tv_sec +
	       (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) / 1e6;
}

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = v >> 24;
	p[1] = v >> 16;
	p[2] = v >> 8;
	p[3] = v;
}

static void put64(unsigned char *p, uint64_t v)
{
	put32(p, v >> 32);
	put32(p + 4, v);
}

static int fail(struct connection *c, const char *fmt, ...)
{
	va_list ap;

	if (!c->error[0]) {
		va_start(ap, fmt);
		vsnprintf(c->error, sizeof(c->error), fmt, ap);
		va_end(ap);
	}
	atomic_store(&c->trial->aborted, true);
	return -1;
}

static bool cancelled(struct trial *t)
{
	return interrupted || atomic_load(&t->aborted);
}

static int ssl_failure(struct connection *c, const char *op, int error,
		       int saved_errno)
{
	unsigned long code = ERR_peek_last_error();
	char reason[256];

	if (code)
		ERR_error_string_n(code, reason, sizeof(reason));
	else if (error == SSL_ERROR_SYSCALL && saved_errno)
		snprintf(reason, sizeof(reason), "%s", strerror(saved_errno));
	else
		snprintf(reason, sizeof(reason), "peer closed or TLS operation failed");
	return fail(c, "%s: SSL error %d: %s", op, error, reason);
}

/* Only the owning worker accesses an SSL object, including WANT_* retries. */
static ssize_t transfer(struct connection *c, struct endpoint *ep, void *buf,
			size_t len, bool writing, short *want)
{
	ssize_t n;

	if (ep->ssl) {
		int ret, error, saved_errno;

		ERR_clear_error();
		errno = 0;
		ret = writing ? SSL_write(ep->ssl, buf, (int)len) :
				SSL_read(ep->ssl, buf, (int)len);
		saved_errno = errno;
		if (ret > 0) {
			*want = writing ? POLLOUT : POLLIN;
			return ret;
		}
		error = SSL_get_error(ep->ssl, ret);
		if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
			*want = error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT;
			return 0;
		}
		if (error == SSL_ERROR_ZERO_RETURN)
			return fail(c, "unexpected TLS EOF");
		return ssl_failure(c, writing ? "TLS write" : "TLS read", error,
				   saved_errno);
	}
	*want = writing ? POLLOUT : POLLIN;
	n = writing ? send(ep->fd, buf, len, MSG_NOSIGNAL) :
		      recv(ep->fd, buf, len, 0);
	if (n > 0)
		return n;
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
		return 0;
	if (!n)
		return fail(c, "unexpected plaintext EOF");
	return fail(c, "plaintext %s: %s", writing ? "write" : "read",
		    strerror(errno));
}

static int wait_fd(struct connection *c, int fd, short events, uint64_t end)
{
	struct pollfd pfd = { .fd = fd, .events = events };

	while (!cancelled(c->trial)) {
		int ret;

		if (now_ns() >= end)
			return fail(c, "connection setup timed out");
		ret = poll(&pfd, 1, 100);
		if (ret > 0) {
			if (pfd.revents & POLLNVAL)
				return fail(c, "invalid socket during setup");
			return 0;
		}
		if (ret < 0 && errno != EINTR)
			return fail(c, "poll: %s", strerror(errno));
	}
	return -1;
}

static int socket_options(int fd)
{
	int one = 1, flags = fcntl(fd, F_GETFL);

	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) ||
	    fcntl(fd, F_SETFD, FD_CLOEXEC) ||
	    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)))
		return -1;
	return 0;
}

static int open_listener(const struct options *o)
{
	struct addrinfo hints = { .ai_family = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM, .ai_flags = AI_PASSIVE };
	struct addrinfo *list, *ai;
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof(addr);
	char host[NI_MAXHOST], service[NI_MAXSERV];
	int fd = -1, one = 1, error;

	error = getaddrinfo(o->backend.host, o->backend.port, &hints, &list);
	if (error) {
		fprintf(stderr, "backend address: %s\n", gai_strerror(error));
		return -1;
	}
	for (ai = list; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (fd < 0)
			continue;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		if (!socket_options(fd) && !bind(fd, ai->ai_addr, ai->ai_addrlen) &&
		    !listen(fd, MAX_CONNECTIONS))
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(list);
	if (fd < 0) {
		perror("backend listen");
		return -1;
	}
	if (getsockname(fd, (struct sockaddr *)&addr, &addrlen) ||
	    getnameinfo((struct sockaddr *)&addr, addrlen, host, sizeof(host),
			service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV)) {
		fprintf(stderr, "cannot report backend listening address\n");
		close(fd);
		return -1;
	}
	printf("# listening=[%s]:%s\n", host, service);
	fflush(stdout);
	return fd;
}

static int connect_proxy(struct connection *c, const struct addrinfo *list,
			 uint64_t end)
{
	const struct addrinfo *ai;
	int saved_errno = ECONNREFUSED;

	for (ai = list; ai && !cancelled(c->trial); ai = ai->ai_next) {
		int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		int ret, error;
		socklen_t len = sizeof(error);

		if (fd < 0) {
			saved_errno = errno;
			continue;
		}
		if (socket_options(fd)) {
			saved_errno = errno;
			close(fd);
			continue;
		}
		ret = connect(fd, ai->ai_addr, ai->ai_addrlen);
		if (!ret) {
			c->tls.fd = fd;
			return 0;
		}
		if (errno == EINPROGRESS) {
			if (wait_fd(c, fd, POLLOUT, end)) {
				close(fd);
				return -1;
			}
			if (!getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len)) {
				if (!error) {
					c->tls.fd = fd;
					return 0;
				}
				errno = error;
			}
		}
		saved_errno = errno;
		close(fd);
	}
	return fail(c, "connect to proxy: %s", strerror(saved_errno));
}

static int tls_handshake(struct connection *c, SSL_CTX *ctx, uint64_t end)
{
	const struct options *o = c->trial->opts;
	const char *name = o->server_name ? o->server_name : o->proxy.host;
	unsigned char addr[sizeof(struct in6_addr)];
	bool numeric = inet_pton(AF_INET, name, addr) == 1 ||
		       inet_pton(AF_INET6, name, addr) == 1;

	c->tls.ssl = SSL_new(ctx);
	if (!c->tls.ssl || !SSL_set_fd(c->tls.ssl, c->tls.fd))
		return fail(c, "cannot create TLS connection");
	if (!numeric && !SSL_set_tlsext_host_name(c->tls.ssl, name))
		return fail(c, "cannot set TLS server name");
	if (!o->insecure) {
		int ret = numeric ?
			X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(c->tls.ssl), name) :
			SSL_set1_host(c->tls.ssl, name);

		if (!ret)
			return fail(c, "cannot set certificate identity check");
	}
	while (!cancelled(c->trial)) {
		int ret, error, saved_errno;

		ERR_clear_error();
		errno = 0;
		ret = SSL_connect(c->tls.ssl);
		saved_errno = errno;
		if (ret == 1)
			return 0;
		error = SSL_get_error(c->tls.ssl, ret);
		if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
			return ssl_failure(c, "TLS handshake", error, saved_errno);
		if (wait_fd(c, c->tls.fd,
			    error == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, end))
			return -1;
	}
	return -1;
}

static int match_backend(struct connection *c, int listener,
			const unsigned char *header, uint64_t end)
{
	unsigned char received[HEADER_SIZE];
	size_t offset;
	short want = POLLOUT;

	for (offset = 0; offset < HEADER_SIZE;) {
		ssize_t n = transfer(c, &c->tls, (void *)(header + offset),
				     HEADER_SIZE - offset, true, &want);

		if (n < 0 || (!n && wait_fd(c, c->tls.fd, want, end)))
			return -1;
		offset += n;
	}
	while (!cancelled(c->trial)) {
		int fd = accept(listener, NULL, NULL);

		if (fd < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
				return fail(c, "accept backend: %s", strerror(errno));
			if (wait_fd(c, listener, POLLIN, end))
				return -1;
			continue;
		}
		if (socket_options(fd)) {
			close(fd);
			return fail(c, "backend socket options: %s", strerror(errno));
		}
		for (offset = 0; offset < HEADER_SIZE;) {
			ssize_t n = recv(fd, received + offset, HEADER_SIZE - offset, 0);

			if (n > 0) {
				offset += n;
				continue;
			}
			/* Ignore a proxy's connect-only health check. */
			if (!n && !offset)
				break;
			if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
				      errno == EINTR)) {
				if (!wait_fd(c, fd, POLLIN, end))
					continue;
			} else {
				fail(c, "backend setup header truncated or unreadable");
			}
			close(fd);
			return -1;
		}
		if (!offset) {
			close(fd);
			continue;
		}
		if (memcmp(header, received, HEADER_SIZE)) {
			close(fd);
			return fail(c, "backend setup header mismatch (wrong connection or corrupted data)");
		}
		c->plain.fd = fd;
		return 0;
	}
	return -1;
}

static EVP_CIPHER_CTX *generator_new(const struct options *o, unsigned int run,
				     unsigned int id, unsigned int direction)
{
	unsigned char material[32] = "tls-proxy-bench", digest[32];
	EVP_CIPHER_CTX *ctx;
	unsigned int len;

	put64(material + 8, o->seed);
	put32(material + 16, run);
	put32(material + 20, id);
	put32(material + 24, direction);
	if (!EVP_Digest(material, sizeof(material), digest, &len, EVP_sha256(), NULL))
		return NULL;
	ctx = EVP_CIPHER_CTX_new();
	if (ctx && !EVP_EncryptInit_ex(ctx, EVP_aes_128_ctr(), NULL, digest, digest + 16)) {
		EVP_CIPHER_CTX_free(ctx);
		ctx = NULL;
	}
	return ctx;
}

static int generate(EVP_CIPHER_CTX *ctx, unsigned char *buf, size_t len)
{
	int produced;

	memset(buf, 0, len);
	return EVP_EncryptUpdate(ctx, buf, &produced, buf, (int)len) &&
	       produced == (int)len ? 0 : -1;
}

static int prepare_flow(struct connection *c, struct flow *f, unsigned int run,
			unsigned int direction)
{
	const struct options *o = c->trial->opts;

	f->name = direction == RX ? "rx" : "tx";
	f->source = direction == RX ? &c->tls : &c->plain;
	f->sink = direction == RX ? &c->plain : &c->tls;
	f->total = o->direction & direction ? o->bytes : 0;
	f->write_want = POLLOUT;
	f->read_want = POLLIN;
	memcpy(f->footer, "TLPBDONE", 8);
	put32(f->footer + 8, c->id);
	put32(f->footer + 12, direction);
	put64(f->footer + 16, f->total);
	put64(f->footer + 24, o->seed ^ run);
	if (!f->total)
		return 0;
	f->out = malloc(o->chunk);
	f->in = malloc(o->chunk);
	f->expected = malloc(o->chunk);
	f->generate = generator_new(o, run, c->id, direction);
	f->reference = generator_new(o, run, c->id, direction);
	if (!f->out || !f->in || !f->expected || !f->generate || !f->reference)
		return fail(c, "cannot allocate payload buffers or generator");
	return 0;
}

static int send_flow(struct connection *c, struct flow *f)
{
	ssize_t n;

	if (f->sent < f->total) {
		if (f->out_off == f->out_len) {
			uint64_t remaining = f->total - f->sent;

			f->out_len = remaining < c->trial->opts->chunk ?
				     (size_t)remaining : c->trial->opts->chunk;
			f->out_off = 0;
			if (generate(f->generate, f->out, f->out_len))
				return fail(c, "%s payload generation failed", f->name);
		}
		n = transfer(c, f->source, f->out + f->out_off,
			     f->out_len - f->out_off, true, &f->write_want);
		if (n < 0)
			return -1;
		f->out_off += n;
		f->sent += n;
		return !!n;
	}
	if (f->footer_out == FOOTER_SIZE)
		return 0;
	n = transfer(c, f->source, f->footer + f->footer_out,
		     FOOTER_SIZE - f->footer_out, true, &f->write_want);
	if (n < 0)
		return -1;
	f->footer_out += n;
	return !!n;
}

static int receive_flow(struct connection *c, struct flow *f)
{
	ssize_t n;
	size_t i;

	if (f->received < f->total) {
		uint64_t remaining = f->total - f->received;
		size_t len = remaining < c->trial->opts->chunk ?
			     (size_t)remaining : c->trial->opts->chunk;

		n = transfer(c, f->sink, f->in, len, false, &f->read_want);
		if (n <= 0)
			return (int)n;
		if (generate(f->reference, f->expected, n))
			return fail(c, "%s reference generation failed", f->name);
		if (memcmp(f->in, f->expected, n)) {
			for (i = 0; i < (size_t)n && f->in[i] == f->expected[i]; i++)
				;
			return fail(c, "%s payload mismatch: offset=%" PRIu64
				    " expected=%02x actual=%02x", f->name,
				    f->received + i, f->expected[i], f->in[i]);
		}
		f->received += n;
		if (f->received == f->total)
			f->finished_ns = now_ns();
		return 1;
	}
	if (f->footer_in < FOOTER_SIZE) {
		unsigned char buf[FOOTER_SIZE];

		n = transfer(c, f->sink, buf, FOOTER_SIZE - f->footer_in, false,
			     &f->read_want);
		if (n <= 0)
			return (int)n;
		if (memcmp(buf, f->footer + f->footer_in, n))
			return fail(c, "%s trailer mismatch after %" PRIu64
				    " payload bytes", f->name, f->received);
		f->footer_in += n;
		return 1;
	}
	return 0;
}

static bool flow_done(const struct flow *f)
{
	return f->footer_out == FOOTER_SIZE && f->footer_in == FOOTER_SIZE;
}

static void poll_flow(struct pollfd *pfds, struct connection *c, struct flow *f)
{
	if (f->footer_out != FOOTER_SIZE)
		pfds[f->source == &c->tls ? 0 : 1].events |= f->write_want;
	if (f->footer_in != FOOTER_SIZE)
		pfds[f->sink == &c->tls ? 0 : 1].events |= f->read_want;
}

static void *worker(void *arg)
{
	struct connection *c = arg;
	struct trial *t = c->trial;
	uint64_t last_progress;

	pthread_mutex_lock(&t->lock);
	t->ready++;
	pthread_cond_broadcast(&t->cond);
	while (!t->go)
		pthread_cond_wait(&t->cond, &t->lock);
	pthread_mutex_unlock(&t->lock);
	last_progress = now_ns();
	while (!cancelled(t) && (!flow_done(&c->rx) || !flow_done(&c->tx))) {
		struct pollfd pfds[2] = { { .fd = c->tls.fd }, { .fd = c->plain.fd } };
		int progress = 0, n;

		n = send_flow(c, &c->rx);
		if (n < 0)
			break;
		progress |= n;
		n = send_flow(c, &c->tx);
		if (n < 0)
			break;
		progress |= n;
		n = receive_flow(c, &c->rx);
		if (n < 0)
			break;
		progress |= n;
		n = receive_flow(c, &c->tx);
		if (n < 0)
			break;
		progress |= n;
		if (progress) {
			last_progress = now_ns();
			continue;
		}
		if (now_ns() - last_progress >= (uint64_t)t->opts->timeout * 1000000000) {
			fail(c, "transfer stalled: rx=%" PRIu64 "/%" PRIu64
			     " tx=%" PRIu64 "/%" PRIu64, c->rx.received, c->rx.total,
			     c->tx.received, c->tx.total);
			break;
		}
		poll_flow(pfds, c, &c->rx);
		poll_flow(pfds, c, &c->tx);
		n = poll(pfds, 2, 100);
		if (n < 0 && errno != EINTR) {
			fail(c, "poll: %s", strerror(errno));
			break;
		}
		if (n > 0 && ((pfds[0].revents | pfds[1].revents) & POLLNVAL)) {
			fail(c, "invalid socket during transfer");
			break;
		}
	}
	return NULL;
}

static void free_flow(struct flow *f)
{
	free(f->out);
	free(f->in);
	free(f->expected);
	EVP_CIPHER_CTX_free(f->generate);
	EVP_CIPHER_CTX_free(f->reference);
}

static const char *direction_name(int direction)
{
	return direction == RX ? "rx" : direction == TX ? "tx" : "bidir";
}

static int run_trial(const struct options *o, SSL_CTX *ctx,
		     const struct addrinfo *proxy, int listener, unsigned int run,
		     bool warmup, double *rate, double *seconds)
{
	struct trial trial = { .opts = o, .lock = PTHREAD_MUTEX_INITIALIZER,
		.cond = PTHREAD_COND_INITIALIZER };
	struct connection *connections = calloc(o->connections, sizeof(*connections));
	unsigned char token[16];
	unsigned int i, started = 0;
	uint64_t start_ns = 0, finish_ns = 0, rx_bytes = 0, tx_bytes = 0;
	double start_cpu = 0, cpu;
	int result = -1;

	atomic_init(&trial.aborted, false);
	if (!connections || RAND_bytes(token, sizeof(token)) != 1) {
		fprintf(stderr, "cannot allocate trial or generate connection token\n");
		free(connections);
		return -1;
	}
	for (i = 0; i < o->connections; i++) {
		connections[i].tls.fd = -1;
		connections[i].plain.fd = -1;
		connections[i].id = i;
		connections[i].trial = &trial;
	}
	for (i = 0; i < o->connections; i++) {
		struct connection *c = &connections[i];
		unsigned char header[HEADER_SIZE] = "TLSPB001";
		uint64_t end = now_ns() + (uint64_t)o->timeout * 1000000000;

		memcpy(header + 8, token, sizeof(token));
		put32(header + 24, i);
		put32(header + 28, o->direction);
		put64(header + 32, o->bytes);
		put64(header + 40, o->seed);
		put32(header + 48, run);
		put32(header + 52, o->chunk);
		if (connect_proxy(c, proxy, end) || tls_handshake(c, ctx, end) ||
		    match_backend(c, listener, header, end) ||
		    prepare_flow(c, &c->rx, run, RX) || prepare_flow(c, &c->tx, run, TX))
			goto out;
		if (!i)
			printf("# %s=%u tls=%s cipher=%s\n", warmup ? "warmup" : "run",
			       warmup ? run + 1 : run - o->warmups + 1,
			       SSL_get_version(c->tls.ssl), SSL_get_cipher_name(c->tls.ssl));
	}
	fflush(stdout);
	for (i = 0; i < o->connections; i++) {
		int error = pthread_create(&connections[i].thread, NULL, worker, &connections[i]);

		if (error) {
			fail(&connections[i], "pthread_create: %s", strerror(error));
			break;
		}
		started++;
	}
	pthread_mutex_lock(&trial.lock);
	while (trial.ready < started)
		pthread_cond_wait(&trial.cond, &trial.lock);
	start_cpu = cpu_seconds();
	start_ns = now_ns();
	trial.go = true;
	pthread_cond_broadcast(&trial.cond);
	pthread_mutex_unlock(&trial.lock);
	for (i = 0; i < started; i++)
		pthread_join(connections[i].thread, NULL);
	if (cancelled(&trial))
		goto out;
	cpu = cpu_seconds() - start_cpu;
	for (i = 0; i < o->connections; i++) {
		struct connection *c = &connections[i];

		if (!flow_done(&c->rx) || !flow_done(&c->tx)) {
			fail(c, "incomplete transfer");
			goto out;
		}
		if (c->rx.finished_ns > finish_ns)
			finish_ns = c->rx.finished_ns;
		if (c->tx.finished_ns > finish_ns)
			finish_ns = c->tx.finished_ns;
		rx_bytes += c->rx.received;
		tx_bytes += c->tx.received;
	}
	*seconds = (finish_ns - start_ns) / 1e9;
	*rate = (rx_bytes + tx_bytes) * 8.0 / (finish_ns - start_ns);
	if (warmup) {
		printf("# warmup=%u seconds=%.6f gbps=%.6f verified=payload\n",
		       run + 1, *seconds, *rate);
	} else {
		printf("%u,%s,%u,%" PRIu64 ",%.6f,%" PRIu64 ",%" PRIu64
		       ",%.6f,%.6f,%.6f,%.6f,payload\n",
		       run - o->warmups + 1, direction_name(o->direction), o->connections,
		       o->bytes, *seconds, rx_bytes, tx_bytes,
		       rx_bytes * 8.0 / (finish_ns - start_ns),
		       tx_bytes * 8.0 / (finish_ns - start_ns), *rate, cpu);
	}
	fflush(stdout);
	result = 0;
out:
	for (i = 0; i < o->connections; i++) {
		struct connection *c = &connections[i];

		if (c->error[0])
			fprintf(stderr, "tls-proxy-bench: trial=%u connection=%u: %s\n",
				run + 1, i, c->error);
		if (!result && c->tls.ssl)
			SSL_shutdown(c->tls.ssl);
		SSL_free(c->tls.ssl);
		if (c->tls.fd >= 0)
			close(c->tls.fd);
		if (c->plain.fd >= 0)
			close(c->plain.fd);
		free_flow(&c->rx);
		free_flow(&c->tx);
	}
	free(connections);
	pthread_cond_destroy(&trial.cond);
	pthread_mutex_destroy(&trial.lock);
	return result;
}

static int parse_address(const char *str, struct address *addr)
{
	const char *host = str, *end, *port;

	if (addr->host)
		return -1;
	if (*str == '[') {
		host++;
		end = strchr(host, ']');
		if (!end || end[1] != ':')
			return -1;
		port = end + 2;
	} else {
		end = strchr(host, ':');
		if (!end || strchr(end + 1, ':'))
			return -1;
		port = end + 1;
	}
	if (end == host || !*port)
		return -1;
	addr->host = strndup(host, end - host);
	addr->port = strdup(port);
	return addr->host && addr->port ? 0 : -1;
}

static int parse_number(const char *str, uint64_t *value, bool suffix)
{
	char *end;
	uint64_t scale = 1, n;

	if (!isdigit((unsigned char)*str))
		return -1;
	errno = 0;
	n = strtoull(str, &end, 0);
	if (errno || end == str)
		return -1;
	if (*end && suffix) {
		switch (*end++) {
		case 'k': case 'K': scale = 1024; break;
		case 'm': case 'M': scale = 1024 * 1024; break;
		case 'g': case 'G': scale = UINT64_C(1024) * 1024 * 1024; break;
		default: return -1;
		}
	}
	if (*end || n > UINT64_MAX / scale)
		return -1;
	*value = n * scale;
	return 0;
}

static void usage(FILE *file)
{
	fprintf(file,
		"Usage: tls-proxy-bench --connect HOST:PORT --listen IP:PORT [options]\n"
		"Run the TLS client and plaintext backend together on this PC.\n"
		"Point the TLS proxy's backend at the --listen address.\n\n"
		"  -d, --direction rx|tx|bidir  Direction relative to proxy (default rx)\n"
		"  -v, --tls 1.2|1.3          Exact TLS version (default 1.3)\n"
		"  -c, --cipher LIST          OpenSSL cipher list / TLS 1.3 ciphersuites\n"
		"  -j, --connections N        Concurrent connections (default 1, max 256)\n"
		"  -n, --bytes N[KMG]         Bytes per connection per direction (default 64M)\n"
		"  -s, --chunk N[KMG]         Application buffer size (default 16K, max 1M)\n"
		"  -r, --runs N               Measured trials (default 3)\n"
		"  -w, --warmups N            Trials excluded from summary (default 1)\n"
		"      --ca FILE             Trust this CA/certificate instead of system CAs\n"
		"      --server-name NAME    TLS SNI and certificate identity\n"
		"      --insecure            Disable certificate verification for lab tests\n"
		"      --timeout SECONDS     Setup / no-progress timeout (default 10)\n"
		"      --seed N              Reproducible payload seed (default 1708171853)\n"
		"      --cpu N               Pin all benchmark threads to one CPU\n"
		"  -h, --help                Show this help\n\n"
		"IPv6 addresses require brackets: [2001:db8::1]:443.\n"
		"Every payload byte is checked; generation and verification are timed.\n"
		"A failure prints its connection, direction and byte offset and exits nonzero.\n");
}

static int compare_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
	enum { OPT_CONNECT = 256, OPT_LISTEN, OPT_CA, OPT_SERVER_NAME, OPT_INSECURE,
	       OPT_TIMEOUT, OPT_SEED, OPT_CPU };
	static const struct option long_opts[] = {
		{ "connect", required_argument, NULL, OPT_CONNECT },
		{ "listen", required_argument, NULL, OPT_LISTEN },
		{ "direction", required_argument, NULL, 'd' },
		{ "tls", required_argument, NULL, 'v' },
		{ "cipher", required_argument, NULL, 'c' },
		{ "connections", required_argument, NULL, 'j' },
		{ "bytes", required_argument, NULL, 'n' },
		{ "chunk", required_argument, NULL, 's' },
		{ "runs", required_argument, NULL, 'r' },
		{ "warmups", required_argument, NULL, 'w' },
		{ "ca", required_argument, NULL, OPT_CA },
		{ "server-name", required_argument, NULL, OPT_SERVER_NAME },
		{ "insecure", no_argument, NULL, OPT_INSECURE },
		{ "timeout", required_argument, NULL, OPT_TIMEOUT },
		{ "seed", required_argument, NULL, OPT_SEED },
		{ "cpu", required_argument, NULL, OPT_CPU },
		{ "help", no_argument, NULL, 'h' },
		{ NULL, 0, NULL, 0 }
	};
	struct options o = { .bytes = 64 * 1024 * 1024, .chunk = 16384,
		.seed = 1708171853, .connections = 1, .runs = 3, .warmups = 1,
		.timeout = 10, .direction = RX, .version = TLS1_3_VERSION, .cpu = -1 };
	struct addrinfo hints = { .ai_family = AF_UNSPEC, .ai_socktype = SOCK_STREAM };
	struct addrinfo *proxy = NULL;
	SSL_CTX *ctx = NULL;
	double *rates = NULL, total_seconds = 0, median;
	uint64_t n;
	unsigned int i;
	int option, listener = -1, result = EXIT_FAILURE, error;

	while ((option = getopt_long(argc, argv, "d:v:c:j:n:s:r:w:h", long_opts, NULL)) != -1) {
		switch (option) {
		case OPT_CONNECT:
			if (parse_address(optarg, &o.proxy))
				goto bad_option;
			break;
		case OPT_LISTEN:
			if (parse_address(optarg, &o.backend))
				goto bad_option;
			break;
		case 'd':
			if (!strcmp(optarg, "rx")) o.direction = RX;
			else if (!strcmp(optarg, "tx")) o.direction = TX;
			else if (!strcmp(optarg, "bidir")) o.direction = RX | TX;
			else goto bad_option;
			break;
		case 'v':
			if (!strcmp(optarg, "1.2")) o.version = TLS1_2_VERSION;
			else if (!strcmp(optarg, "1.3")) o.version = TLS1_3_VERSION;
			else goto bad_option;
			break;
		case 'c': o.cipher = optarg; break;
		case OPT_CA: o.ca = optarg; break;
		case OPT_SERVER_NAME: o.server_name = optarg; break;
		case OPT_INSECURE: o.insecure = true; break;
		case 'n': case 's':
			if (parse_number(optarg, &n, true) || !n)
				goto bad_option;
			if (option == 'n') o.bytes = n;
			else if (n <= MAX_CHUNK) o.chunk = n;
			else goto bad_option;
			break;
		case 'j': case 'r': case 'w': case OPT_TIMEOUT: case OPT_SEED: case OPT_CPU:
			if (parse_number(optarg, &n, false))
				goto bad_option;
			if (option == OPT_SEED) o.seed = n;
			else if (option == 'j' && n && n <= MAX_CONNECTIONS) o.connections = n;
			else if (option == 'r' && n && n <= 10000) o.runs = n;
			else if (option == 'w' && n <= 10000) o.warmups = n;
			else if (option == OPT_TIMEOUT && n && n <= 3600) o.timeout = n;
			else if (option == OPT_CPU && n < CPU_SETSIZE) o.cpu = n;
			else goto bad_option;
			break;
		case 'h': usage(stdout); result = EXIT_SUCCESS; goto out;
		default: goto bad_option;
		}
	}
	if (optind != argc || !o.proxy.host || !o.backend.host ||
	    o.bytes > UINT64_MAX / (2 * o.connections)) {
		fprintf(stderr, "missing address, unexpected argument, or byte count overflow\n");
		goto bad_option;
	}
	if (o.cpu >= 0) {
		cpu_set_t cpus;

		CPU_ZERO(&cpus);
		CPU_SET(o.cpu, &cpus);
		if (sched_setaffinity(0, sizeof(cpus), &cpus)) {
			perror("CPU affinity");
			goto out;
		}
	}
	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	ctx = SSL_CTX_new(TLS_client_method());
	if (!ctx || !SSL_CTX_set_min_proto_version(ctx, o.version) ||
	    !SSL_CTX_set_max_proto_version(ctx, o.version))
		goto ssl_error;
	SSL_CTX_set_mode(ctx, SSL_MODE_ENABLE_PARTIAL_WRITE);
	SSL_CTX_set_verify(ctx, o.insecure ? SSL_VERIFY_NONE : SSL_VERIFY_PEER, NULL);
	if (!o.insecure && !(o.ca ? SSL_CTX_load_verify_locations(ctx, o.ca, NULL) :
				   SSL_CTX_set_default_verify_paths(ctx)))
		goto ssl_error;
	if (o.version == TLS1_3_VERSION) {
		if (!SSL_CTX_set_ciphersuites(ctx, o.cipher ? o.cipher : "TLS_AES_128_GCM_SHA256"))
			goto ssl_error;
	} else if (!SSL_CTX_set_cipher_list(ctx, o.cipher ? o.cipher :
			"ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256")) {
		goto ssl_error;
	}
	error = getaddrinfo(o.proxy.host, o.proxy.port, &hints, &proxy);
	if (error) {
		fprintf(stderr, "proxy address: %s\n", gai_strerror(error));
		goto out;
	}
	rates = calloc(o.runs, sizeof(*rates));
	if (!rates) {
		perror("allocate results");
		goto out;
	}
	listener = open_listener(&o);
	if (listener < 0)
		goto out;
	printf("# direction=%s connections=%u bytes_per_connection_per_direction=%" PRIu64
	       " chunk=%u runs=%u warmups=%u seed=%" PRIu64 " cpu=%d\n",
	       direction_name(o.direction), o.connections, o.bytes, o.chunk,
	       o.runs, o.warmups, o.seed, o.cpu);
	printf("# TLS=%s verify_certificate=%s payload=AES-128-CTR(seed,trial,connection,direction)\n",
	       OpenSSL_version(OPENSSL_VERSION), o.insecure ? "no" : "yes");
	printf("# Timing excludes setup; includes payload generation, transport and byte verification.\n");
	printf("run,direction,connections,bytes_per_connection,seconds,rx_bytes,tx_bytes,"
	       "rx_gbps,tx_gbps,total_gbps,cpu_seconds,verified\n");
	fflush(stdout);
	for (i = 0; i < o.warmups + o.runs; i++) {
		double rate, seconds;

		if (run_trial(&o, ctx, proxy, listener, i, i < o.warmups, &rate, &seconds)) {
			fprintf(stderr, "tls-proxy-bench: FAIL%s\n", interrupted ? " (interrupted)" : "");
			goto out;
		}
		if (i >= o.warmups) {
			rates[i - o.warmups] = rate;
			total_seconds += seconds;
		}
	}
	qsort(rates, o.runs, sizeof(*rates), compare_double);
	median = o.runs & 1 ? rates[o.runs / 2] :
		 (rates[o.runs / 2 - 1] + rates[o.runs / 2]) / 2;
	printf("# PASS gbps min=%.6f median=%.6f max=%.6f aggregate=%.6f verified=payload\n",
	       rates[0], median, rates[o.runs - 1],
	       (double)o.bytes * o.connections * o.runs *
	       (o.direction == (RX | TX) ? 2 : 1) * 8 / total_seconds / 1e9);
	result = EXIT_SUCCESS;
	goto out;
bad_option:
	fprintf(stderr, "invalid command line%s%s\n", optarg ? ": " : "", optarg ? optarg : "");
	usage(stderr);
	goto out;
ssl_error:
	fprintf(stderr, "TLS configuration failed\n");
	ERR_print_errors_fp(stderr);
out:
	if (listener >= 0)
		close(listener);
	SSL_CTX_free(ctx);
	if (proxy)
		freeaddrinfo(proxy);
	free(rates);
	free(o.proxy.host);
	free(o.proxy.port);
	free(o.backend.host);
	free(o.backend.port);
	return result;
}
