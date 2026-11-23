#include <linux/cleanup.h>
#include <linux/inet.h>
#include <linux/moduleparam.h>
#include <linux/net.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include "http.h"

static char *server_ip = "127.0.0.1";
static ushort server_port = 8080;
module_param(server_ip, charp, 0444);
module_param(server_port, ushort, 0444);

static int vtfs_http_exchange(struct socket *sock, char *request, char *reply,
			       size_t capacity, void *response, size_t size)
{
	char *body;
	size_t offset = 0, length;
	struct msghdr msg = {};
	struct kvec vec;
	__le64 status;
	int err;

	length = strlen(request);
	while (offset < length) {
		vec.iov_base = request + offset;
		vec.iov_len = length - offset;
		err = kernel_sendmsg(sock, &msg, &vec, 1, vec.iov_len);
		if (err <= 0)
			return err ?: -EIO;
		offset += err;
	}

	offset = 0;
	while (offset < capacity) {
		memset(&msg, 0, sizeof(msg));
		vec.iov_base = reply + offset;
		vec.iov_len = capacity - offset;
		err = kernel_recvmsg(sock, &msg, &vec, 1, vec.iov_len, 0);
		if (err < 0)
			return err;
		if (!err)
			break;
		offset += err;
	}
	reply[offset] = '\0';
	body = strstr(reply, "\r\n\r\n");
	if (strncmp(reply, "HTTP/1.0 200 ", 13) || !body)
		return -EPROTO;
	body += 4;
	length = reply + offset - body;
	if (length < sizeof(status))
		return -EPROTO;
	memcpy(&status, body, sizeof(status));
	if (le64_to_cpu(status))
		return le64_to_cpu(status) <= MAX_ERRNO ? -(int)le64_to_cpu(status) : -EPROTO;
	if (length != sizeof(status) + size)
		return -EPROTO;
	if (size)
		memcpy(response, body + sizeof(status), size);
	return 0;
}

int vtfs_http_call(const char *token, const char *method, const char *argument,
		   void *response, size_t size)
{
	struct sockaddr_in addr = { .sin_family = AF_INET };
	struct socket *sock;
	char *request __free(kfree) = NULL;
	char *reply __free(kfree) = NULL;
	size_t capacity = size + 1024;
	int err;

	addr.sin_port = htons(server_port);
	if (!in4_pton(server_ip, -1, (u8 *)&addr.sin_addr, -1, NULL))
		return -EINVAL;

	request = kasprintf(GFP_KERNEL,
			    "POST /api/%s?token=%s&arg=%s HTTP/1.0\r\n"
			    "Host: %s\r\nContent-Length: 0\r\n\r\n",
			    method, token, argument, server_ip);
	if (!request)
		return -ENOMEM;
	reply = kmalloc(capacity + 1, GFP_KERNEL);
	if (!reply)
		return -ENOMEM;

	err = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);
	if (err)
		return err;
	sock->sk->sk_sndtimeo = 5 * HZ;
	sock->sk->sk_rcvtimeo = 5 * HZ;
	err = kernel_connect(sock, (struct sockaddr *)&addr, sizeof(addr), 0);
	if (!err)
		err = vtfs_http_exchange(sock, request, reply, capacity, response, size);
	sock_release(sock);
	return err;
}
