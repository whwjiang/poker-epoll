#include <gtest/gtest.h>

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server.h"

namespace {

TEST(Server, RemovesEmptyTableAfterLastDisconnect) {
  int epfd = epoll_create1(0);
  ASSERT_GE(epfd, 0);
  int listenfd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(listenfd, 0);

  Server server(listenfd);

  int pair1[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, pair1), 0);
  auto first = server.handle_connect(pair1[0]);
  ASSERT_TRUE(first.result.has_value());
  const auto first_table_id = first.conn->table_id;
  server.handle_close(first.conn->player_id);
  close(pair1[1]);

  int pair2[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, pair2), 0);
  auto second = server.handle_connect(pair2[0]);
  ASSERT_TRUE(second.result.has_value());
  EXPECT_NE(second.conn->table_id, first_table_id);
  server.handle_close(second.conn->player_id);
  close(pair2[1]);
}

} // namespace
