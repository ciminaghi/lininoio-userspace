#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>

/* http://blog.varunajayasiri.com/passing-file-descriptors-between-processes-using-sendmsg-and-recvmsg */
int send_file_descriptor(int socket, void *buf, int len, int fd)
{
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *control_message = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	int *ptr;

	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

	iov[0].iov_base = buf;
	iov[0].iov_len = len;

	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_iov = iov;
	message.msg_iovlen = 1;
	message.msg_controllen =  CMSG_SPACE(sizeof(int));
	message.msg_control = ctrl_buf;

	control_message = CMSG_FIRSTHDR(&message);
	control_message->cmsg_level = SOL_SOCKET;
	control_message->cmsg_type = SCM_RIGHTS;
	control_message->cmsg_len = CMSG_LEN(sizeof(int));

	ptr = ((int *) CMSG_DATA(control_message));
	*ptr = fd;

	return sendmsg(socket, &message, 0);
}

int recv_fd(int socket, void *buffer, int *len, const char *sig, int siglen)
{
	int *ptr;
	struct msghdr socket_message;
	struct iovec io_vector[1];
	struct cmsghdr *control_message = NULL;
	char ancillary_element_buffer[CMSG_SPACE(sizeof(int))];

	/* start clean */
	memset(&socket_message, 0, sizeof(struct msghdr));
	memset(ancillary_element_buffer, 0, CMSG_SPACE(sizeof(int)));

	/* setup a place to fill in message contents */
	io_vector[0].iov_base = buffer;
	io_vector[0].iov_len = *len;
	socket_message.msg_iov = io_vector;
	socket_message.msg_iovlen = 1;

	/* provide space for the ancillary data */
	socket_message.msg_control = ancillary_element_buffer;
	socket_message.msg_controllen = CMSG_SPACE(sizeof(int));

	if(recvmsg(socket, &socket_message, MSG_CMSG_CLOEXEC) < 0)
		return -1;

	if (memcmp(buffer, sig, siglen))
		/* signature not found */
		return -1;

	if((socket_message.msg_flags & MSG_CTRUNC) == MSG_CTRUNC)
		/*
		 * we did not provide enough space for the
		 * ancillary element array
		 */
		return -1;

	/* iterate ancillary elements */
	for(control_message = CMSG_FIRSTHDR(&socket_message);
	    control_message != NULL;
	    control_message = CMSG_NXTHDR(&socket_message, control_message)) {
		if( (control_message->cmsg_level == SOL_SOCKET) &&
		    (control_message->cmsg_type == SCM_RIGHTS) ) {
			ptr = ((int *) CMSG_DATA(control_message));
			return *ptr;
		}
	}

	return -1;
}
