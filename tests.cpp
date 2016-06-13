#include <gtest/gtest.h>
#include <iostream>
#include <thread>
#include <semaphore.h>
#include "lib.c"
#define TESTING
sem_t server_sem;
#include "tls.c"

/* Sends a short message using send(), and checks its return value */
void test_send_small_encrypt(int opfd, void *unused) {

    char const*test_str = "test_send";
    int to_send = strlen(test_str) + 1;
    ASSERT_EQ(send(opfd, test_str, to_send, 0), to_send)
        << "Incorrect number of bytes sent" ;
}

/* Sends a short file using sendfile(), and checks its return */
void test_sendfile_small_encrypt(int opfd, void *unused) {
    int filefd = open("small.txt", O_RDONLY);
    ASSERT_NE(filefd, -1) << "Open failed" ;
    struct stat st;
    fstat(filefd, &st);
    ASSERT_GE(sendfile(opfd, filefd, 0, st.st_size), 0)
        << "Sendfile FAILED";
}

/* Sends a short message and read the reply, which should echo the send message
 * Checks that the message was sent and received correctly
 */
void test_recv_small_decrypt(int opfd, void *unused) {
    char const *test_str = "test_read";
    int send_len = strlen(test_str) + 1;
    char buf[4096];
    ASSERT_EQ(send(opfd, test_str, send_len, 0), send_len)
        << "Incorrect number of bytes sent" ;
    ASSERT_NE(recv(opfd, buf, send_len, 0), -1) << "Recv failed";
    ASSERT_STREQ(test_str, buf);
}

void test_sendmsg_single(int opfd, void *unused) {
    // Load up the cmsg data.
    struct cmsghdr *header = nullptr;
    uint32_t *type = nullptr;
    struct msghdr msg;

    /* IV data */
    struct af_alg_iv *alg_iv = nullptr;
    int ivsize = 12;
    uint32_t iv_msg_size = CMSG_SPACE(sizeof(*alg_iv) + ivsize);

    /* AEAD data */
    uint32_t *assoclen = nullptr;
    uint32_t assoc_msg_size = CMSG_SPACE(sizeof(*assoclen));

    uint32_t bufferlen = CMSG_SPACE(sizeof(*type)) + /*Encryption/Decryption*/
    iv_msg_size + /* IV */
    assoc_msg_size;/* AEAD associated data size */

    memset(&msg, 0, sizeof(msg));
    char* buffer = (char *) calloc(1, bufferlen);
    msg.msg_control = buffer;
    msg.msg_controllen = bufferlen;
    char const *test_str = "test_sendmsg";
    size_t send_len = strlen(test_str) + 1;
    struct iovec vec = { (void *) test_str, send_len };
    msg.msg_iov = &vec;
    msg.msg_iovlen = 1;
    ASSERT_EQ(sendmsg(opfd, &msg, 0), send_len)
        << "Incorrect number of bytes sent";
    char buf[4096];
    ASSERT_NE(recv(opfd, buf, send_len, 0), -1) << "Recv failed";
    ASSERT_STREQ(test_str, buf);
    free(buffer);
}

void test_sendmsg_multiple(int opfd, void *unused) {
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));

    //Load up the send data
    int iov_len = 5;
    char *test_strs[iov_len];
    struct iovec vec[iov_len];
    int total_len = 0;
    char const *test_str = "test_sendmsg_multiple";
    for (int i = 0; i < iov_len; i++) {
        test_strs[i] = (char *) malloc(strlen(test_str) + 1);
        snprintf(test_strs[i], strlen(test_str) + 1, "%s", test_str);
        vec[i].iov_base = (void *) test_strs[i];
        vec[i].iov_len = strlen(test_strs[i]) + 1;
        total_len += vec[i].iov_len;
    }
    msg.msg_iov = vec;
    msg.msg_iovlen = iov_len;

    // Load up the cmsg data
    struct cmsghdr *header = nullptr;
    uint32_t *type = nullptr;
    /* IV data */
    struct af_alg_iv *alg_iv = nullptr;
    int ivsize = 12;
    uint32_t iv_msg_size = CMSG_SPACE(sizeof(*alg_iv) + ivsize);

    /* AEAD data */
    uint32_t *assoclen = nullptr;
    uint32_t assoc_msg_size = CMSG_SPACE(sizeof(*assoclen));

    uint32_t bufferlen = CMSG_SPACE(sizeof(*type)) + /*Encryption/Decryption*/
    iv_msg_size + /* IV */
    assoc_msg_size;/* AEAD associated data size */

    char* buffer = (char *) calloc(1, bufferlen);
    msg.msg_control = buffer;
    msg.msg_controllen = bufferlen;

    ASSERT_EQ(sendmsg(opfd, &msg, 0), total_len)
        << "Incorrect number of bytes sent" ;
    char buf[4096];
    ASSERT_NE(recv(opfd, buf, total_len, 0), -1) << "Recv failed";
    int len_cmp = 0;
    for (int i = 0; i < iov_len; i++) {
        ASSERT_STREQ(test_strs[i], buf + len_cmp);
        len_cmp += strlen(buf + len_cmp) + 1;
    }
    free(buffer);
    for(int i=0;i<iov_len;i++)
        free(test_strs[i]);
}

/* Test sendmsg where iovecs point to memory scattered across
 * physical memory
 */
void test_sendmsg_multiple_scattered(int opfd, void *unused) {
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));

    //Load up the send data
    int iov_len = 3;
    struct iovec vec[iov_len];
    int total_len = 0;
    char test_stack[] = "test_sendmsg_stack";
    char const *test_data = "test_sendmsg_data";
    char const *test_heap = "test_sendmsg_heap";
    char *heap = (char *) malloc(strlen(test_heap) + 1);
    snprintf(heap, strlen(test_heap) + 1, "%s", test_heap);
    vec[0].iov_base = (void *) test_stack; 
    vec[0].iov_len = strlen(test_stack) + 1;
    total_len += vec[0].iov_len;
    vec[1].iov_base = (void *) test_data; 
    vec[1].iov_len = strlen(test_data) + 1;
    total_len += vec[1].iov_len;
    vec[2].iov_base = (void *) test_heap; 
    vec[2].iov_len = strlen(test_heap) + 1;
    total_len += vec[2].iov_len;
    msg.msg_iov = vec;
    msg.msg_iovlen = iov_len;

    // Load up the cmsg data
    struct cmsghdr *header = nullptr;
    uint32_t *type = nullptr;
    /* IV data */
    struct af_alg_iv *alg_iv = nullptr;
    int ivsize = 12;
    uint32_t iv_msg_size = CMSG_SPACE(sizeof(*alg_iv) + ivsize);

    /* AEAD data */
    uint32_t *assoclen = nullptr;
    uint32_t assoc_msg_size = CMSG_SPACE(sizeof(*assoclen));

    uint32_t bufferlen = CMSG_SPACE(sizeof(*type)) + /*Encryption/Decryption*/
    iv_msg_size + /* IV */
    assoc_msg_size;/* AEAD associated data size */

    char* buffer = (char *) calloc(1, bufferlen);
    msg.msg_control = buffer;
    msg.msg_controllen = bufferlen;

    ASSERT_EQ(sendmsg(opfd, &msg, 0), total_len)
        << "Incorrect number of bytes sent" ;
    char buf[4096];
    ASSERT_NE(recv(opfd, buf, total_len, 0), -1) << "Recv failed";
    int len_cmp = 0;
    ASSERT_STREQ(test_stack, buf + len_cmp);
    len_cmp += vec[0].iov_len;
    ASSERT_STREQ(test_data, buf + len_cmp);
    len_cmp += vec[1].iov_len;
    ASSERT_STREQ(test_heap, buf + len_cmp);
    free(buffer);
    free(heap);
}

/* Send 1<<14 amount of data using 1024 (max) iovecs */
void test_sendmsg_multiple_stress(int opfd, void *unused) {
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));

    //Load up the send data
    int iov_len = 1024;
    char *test_strs[iov_len];
    struct iovec vec[iov_len];
    int total_len = 0;
    char const *test_str = "abcdefghijklmno";
    for (int i = 0; i < iov_len; i++) {
        test_strs[i] = (char *) malloc(strlen(test_str) + 1);
        snprintf(test_strs[i], strlen(test_str) + 1, "%s", test_str);
        vec[i].iov_base = (void *) test_strs[i];
        vec[i].iov_len = strlen(test_strs[i]) + 1;
        total_len += vec[i].iov_len;
    }
    msg.msg_iov = vec;
    msg.msg_iovlen = iov_len;

    // Load up the cmsg data
    struct cmsghdr *header = nullptr;
    uint32_t *type = nullptr;
    /* IV data */
    struct af_alg_iv *alg_iv = nullptr;
    int ivsize = 12;
    uint32_t iv_msg_size = CMSG_SPACE(sizeof(*alg_iv) + ivsize);

    /* AEAD data */
    uint32_t *assoclen = nullptr;
    uint32_t assoc_msg_size = CMSG_SPACE(sizeof(*assoclen));

    uint32_t bufferlen = CMSG_SPACE(sizeof(*type)) + /*Encryption/Decryption*/
    iv_msg_size + /* IV */
    assoc_msg_size;/* AEAD associated data size */

    char* buffer = (char *) calloc(1, bufferlen);
    msg.msg_control = buffer;
    msg.msg_controllen = bufferlen;

    ASSERT_EQ(sendmsg(opfd, &msg, 0), total_len)
        << "Incorrect number of bytes sent" ;
    char buf[1<<14];
    ASSERT_NE(recv(opfd, buf, total_len, 0), -1) << "Recv failed";
    int len_cmp = 0;
    for (int i = 0; i < iov_len; i++) {
        ASSERT_STREQ(test_strs[i], buf + len_cmp);
        len_cmp += strlen(buf + len_cmp) + 1;
    }
    free(buffer);
    for(int i=0;i<iov_len;i++)
        free(test_strs[i]);
}

void test_recvmsg_single(int opfd, void *unused) {
    char const *test_str = "test_recvmsg_single";
    int send_len = strlen(test_str) + 1;
    ASSERT_EQ(send(opfd, test_str, send_len, 0), send_len)
        << "Incorrect number of bytes sent" ;
    char buf[4096];
    struct iovec vec;
    vec.iov_base = (char *)buf;
    vec.iov_len = 4096;
    struct msghdr hdr;
    hdr.msg_iovlen = 1;
    hdr.msg_iov = &vec;  
    ASSERT_NE(recvmsg(opfd, &hdr, 0), -1) << "Recv failed";
    ASSERT_STREQ(test_str, buf);
}

void test_recvmsg_multiple(int opfd, void *unused) {
    char buf[1<<14];
    int send_len = 1<<14;
    gen_random(buf, send_len);
    ASSERT_EQ(send(opfd, buf, send_len, 0), send_len)
        << "Incorrect number of bytes sent" ;
    unsigned int msg_iovlen = 1024;
    unsigned int iov_len = 16;
    struct iovec vec[msg_iovlen];
    char *iov_base[msg_iovlen];
    for(int i=0;i<msg_iovlen;i++)
    {
        iov_base[i] = (char *)malloc(iov_len);
        vec[i].iov_base = iov_base[i];
        vec[i].iov_len = iov_len;
    }
    struct msghdr hdr;
    hdr.msg_iovlen = msg_iovlen;
    hdr.msg_iov = vec;  
    ASSERT_NE(recvmsg(opfd, &hdr, 0), -1) << "Recv failed";
    unsigned int len_compared = 0;
    for(int i=0;i<msg_iovlen;i++)
        ASSERT_EQ(memcmp(buf + len_compared, iov_base[i], iov_len), 0);
    for(int i=0;i<msg_iovlen;i++)
        free(iov_base[i]);
}

void test_recv_partial(int opfd, void *unused) {
    char const *test_str = "test_read_partial";
    char const *test_str_first = "test_read";
    char const *test_str_second = "_partial";
    int send_len = strlen(test_str) + 1;
    char buf[4096];
    memset(buf, 0, 4096);
    ASSERT_EQ(send(opfd, test_str, send_len, 0), send_len)
        << "Incorrect number of bytes sent" ;
    ASSERT_NE(recv(opfd, buf, strlen(test_str_first), 0), -1) << "1st half of recv failed";
    ASSERT_STREQ(test_str_first, buf);
    ASSERT_NE(recv(opfd, buf, strlen(test_str_second), 0), -1) << "2nd half of recv failed";
    ASSERT_STREQ(test_str_second, buf);
}
pthread_t server_thread;
using namespace std;
class MyTestSuite: public testing::Test {
protected:
    static void SetUpTestCase() {
        SSL_library_init();
        OpenSSL_add_all_algorithms();
        ERR_load_BIO_strings();
        ERR_load_crypto_strings();
        SSL_load_error_strings();/* load all error messages */
        sem_init(&server_sem, 0, 0);
        thread t1(main_server);
        t1.detach();
        sem_wait(&server_sem);
    }
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }
};

TEST_F(MyTestSuite, send_small_encrypt)
{
    main_test_client(test_send_small_encrypt);
}

TEST_F(MyTestSuite, sendfile_small_encrypt)
{
    main_test_client(test_sendfile_small_encrypt);
}

TEST_F(MyTestSuite, read_small_decrypt)
{
    main_test_client(test_recv_small_decrypt);
}

TEST_F(MyTestSuite, DISABLED_socketpair)
{
    ASSERT_EQ(1, 0)
        ;
}

TEST_F(MyTestSuite, DISABLED_bind)
{
    ASSERT_EQ(1, 0)
        ;
}

TEST_F(MyTestSuite, DISABLED_getsockname)
{
    ASSERT_EQ(1, 0)
        ;
}

TEST_F(MyTestSuite, DISABLED_sendto)
{
    ASSERT_EQ(1, 0)
        ;
}

TEST_F(MyTestSuite, DISABLED_recvfrom)
{
    ASSERT_EQ(1, 0)
        ;
}

TEST_F(MyTestSuite, sendmsg)
{
    main_test_client(test_sendmsg_single);
}

TEST_F(MyTestSuite, sendmsg_multiple_iovecs)
{
    main_test_client(test_sendmsg_multiple);
}

TEST_F(MyTestSuite, sendmsg_multiple_iovecs_scattered)
{
    main_test_client(test_sendmsg_multiple_scattered);
}

TEST_F(MyTestSuite, sendmsg_multiple_iovecs_stress)
{
    main_test_client(test_sendmsg_multiple_stress);
}


TEST_F(MyTestSuite, DISABLED_sendmmsg)
{
    ASSERT_EQ(1, 0)
        ;
}

TEST_F(MyTestSuite, recvmsg)
{
    main_test_client(test_recvmsg_single);
}

TEST_F(MyTestSuite, DISABLED_recvmsg_multiple)
{
    main_test_client(test_recvmsg_multiple);
}

TEST_F(MyTestSuite, DISABLED_recv_partial)
{
    main_test_client(test_recv_partial);
}

TEST_F(MyTestSuite, DISABLED_getsockopt)
{
    ASSERT_EQ(1, 0)
        ;
}

TEST_F(MyTestSuite, DISABLED_setsockopt)
{
    ASSERT_EQ(1, 0)
        ;
}
