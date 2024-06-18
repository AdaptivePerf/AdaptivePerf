// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "mocks.hpp"
#include "consts.hpp"
#include <gtest/gtest.h>
#include <cmath>

// Repeating each test multiple times increases the chance of
// detecting race-condition-related bugs if all other
// analyses failed to spot them beforehand
#ifndef CLIENT_TEST_REPEAT
#define CLIENT_TEST_REPEAT 10000
#endif

using namespace testing;
using namespace std::chrono_literals;


class StdClientTest : public Test {
protected:
  fs::path result_path;
  StdClientTest() : result_path("test_result_dir") { }
  ~StdClientTest() { fs::remove_all(this->result_path); }
};

TEST_F(StdClientTest, StandardCommTest) {
  for (int i = 0; i < CLIENT_TEST_REPEAT; i++) {
    const fs::path result_path("test_result_dir");
    const unsigned long long file_timeout_seconds = 124941;
    const std::string result_dir = result_path.filename();
    const std::string profiled_filename = "test_command123";
    const unsigned int buf_size = 1024;
    const int subclients = 4;

    int created_subclients = 0;

    nlohmann::json results[subclients];

    std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
      std::make_unique<test::MockSubclient::Factory>([&](test::MockSubclient &s) {
        std::string result_str = "{}";

        switch (created_subclients) {
        case 0:
          result_str = "{\"<SYSCALL_TREE>\": [12894, [\"300\", \"301\", \"302\", "
            "\"305\"],{\"300\": {\"parent\": null, \"tag\": "
            "[\"test_command\", \"300/300\", 0, 568]}, "
            "\"301\": {\"parent\": \"300\", \"tag\": "
            "[\"test_command\", \"300/301\", 585, 1004]}, "
            "\"302\": {\"parent\": \"300\", \"tag\": [\"sleep\", "
            "\"302/302\", 1002, 67]}, "
            "\"305\": {\"parent\": \"301\", \"tag\": "
            "[\"test_command\", \"300/305\", 1050, 1]}}]}";
          break;

        case 1:
          result_str =
            "{\"<SAMPLE>\": {"
            "\"300_300\": {\"first_time\": 12894, \"sampled_time\": 18284, "
            "\"offcpu_regions\": "
            "[[12895, 5], [13594, 999], [15894, 128]], "
            "\"walltime\": [\"dummy4\", \"dummy5\", \"dummy6\", \"dummy7\"], "
            "\"page-faults\": {\"dummy0\": 1, \"dummy9\": 2, \"dummy10\": "
            "3}}, "
            "\"401_402\": {\"first_time\": 15681, \"sampled_time\": 1782, "
            "\"offcpu_regions\": [], "
            "\"walltime\": {}}, "
            "\"278_288\": {\"first_time\": 14, \"sampled_time\": 2, "
            "\"offcpu_regions\": [], "
            "\"walltime\": {}}}}";
          break;

        case 2:
          // Nothing
          break;

        case 3:
          result_str =
            "{\"<SYSCALL>\": {\"300\": [\"x\", \"y\", \"z\"], \"305\": "
            "[\"@\"], \"302\": [\"y\", \"*\"]}, "
            "\"<SAMPLE>\": {"
            "\"302_302\": {\"first_time\": 13000, \"sampled_time\": 100, "
            "\"offcpu_regions\": [], \"walltime\": [\"dummy11\"], "
            "\"page-faults\": []}, \"300_305\": {\"first_time\": 13001,"
            "\"sampled_time\": 585, \"offcpu_regions\": [[18753, 100]], "
            "\"walltime\": {}, "
            "\"page-faults\": [\"dummy22\", \"dummy000\"]}}}";
          break;

        default:
          // Nothing
          break;
        }

        int index = created_subclients++;
        results[index] = nlohmann::json::parse(result_str);

        EXPECT_CALL(s, construct(_, profiled_filename, buf_size)).Times(1);
        EXPECT_CALL(s, real_process).Times(1);
        EXPECT_CALL(s, get_connection_instructions)
          .Times(1)
          .WillRepeatedly(Return(std::to_string(created_subclients)));
        EXPECT_CALL(s, get_result).Times(1)
          .WillRepeatedly(ReturnRef(results[index]));
      }, true);

    std::unique_ptr<aperf::Connection> mock_connection =
      std::make_unique<StrictMock<test::MockConnection> >();

    test::MockConnection &connection = *((test::MockConnection *)
                                         mock_connection.get());

    EXPECT_CALL(connection, get_buf_size).Times(AtLeast(1))
      .WillRepeatedly(Return(buf_size));

    {
      InSequence sequence;
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(2)
        .WillOnce(Return("start" + std::to_string(subclients) + " " + result_dir))
        .WillOnce(Return(profiled_filename));
      EXPECT_CALL(connection, write("mock 1 2 3 4", true)).Times(1);
      EXPECT_CALL(connection, write("start_profile", true)).Times(1);
      EXPECT_CALL(connection, write("out_files", true)).Times(1);
      EXPECT_CALL(connection, read(NO_TIMEOUT)).Times(1)
        .WillOnce(Return("o out1.dat"));
      EXPECT_CALL(connection, read(_, _, file_timeout_seconds)).Times(1)
        .WillOnce([&](char *buf, unsigned int bytes, long timeout) {
          buf[0] = 'a';
          buf[1] = 'b';
          buf[2] = 'c';
          buf[3] = 'd';
          buf[4] = 'e';
          buf[5] = '1';
          buf[6] = '2';
          buf[7] = '3';
          buf[8] = '4';
          buf[9] = '5';
          buf[10] = 0x03;
          return 11;
        });
      EXPECT_CALL(connection, read(NO_TIMEOUT)).Times(1)
        .WillOnce(Return("p proc1.dat"));
      EXPECT_CALL(connection, read(_, _, file_timeout_seconds)).Times(1)
        .WillOnce([&](char *buf, unsigned int bytes, long timeout) {
          buf[0] = ' ';
          buf[1] = '!';
          buf[2] = 0x03;
          return 3;
        });
      EXPECT_CALL(connection, read(NO_TIMEOUT)).Times(1)
        .WillOnce(Return("o out2.dat"));
      EXPECT_CALL(connection, read(_, _, file_timeout_seconds)).Times(1)
        .WillOnce([&](char *buf, unsigned int bytes, long timeout) {
          buf[0] = 0x03;
          return 1;
        });
      EXPECT_CALL(connection, read(NO_TIMEOUT)).Times(1)
        .WillOnce(Return("o out3.dat"));
      EXPECT_CALL(connection, read(_, _, file_timeout_seconds)).Times(1)
        .WillOnce([&](char *buf, unsigned int bytes, long timeout) {
          buf[0] = 'X';
          buf[1] = '@';
          buf[2] = '?';
          buf[3] = 0x03;
          return 4;
        });
      EXPECT_CALL(connection, read(NO_TIMEOUT)).Times(1)
        .WillOnce(Return("p proc2.dat"));
      EXPECT_CALL(connection, read(_, _, file_timeout_seconds)).Times(1)
        .WillOnce([&](char *buf, unsigned int bytes, long timeout) {
          buf[0] = 'O';
          buf[1] = 'p';
          buf[2] = '%';
          buf[3] = '%';
          buf[4] = 'b';
          buf[5] = '+';
          buf[6] = 0x03;
          return 7;
        });
      EXPECT_CALL(connection, read(NO_TIMEOUT)).Times(1)
        .WillOnce(Return("<STOP>"));

      EXPECT_CALL(connection, write("finished", true)).Times(1);
      EXPECT_CALL(connection, close).Times(1);
    }

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock may seg fault otherwise).
    {
      aperf::StdClient::Factory factory(subclient_factory);
      std::unique_ptr<aperf::Client> client = factory.make_client(mock_connection,
                                                                  file_timeout_seconds);
      client->process();
    }

    ASSERT_EQ(created_subclients, subclients);

    ASSERT_TRUE(fs::is_directory(result_path));
    ASSERT_TRUE(fs::is_directory(result_path / "out"));
    ASSERT_TRUE(fs::is_directory(result_path / "processed"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "out" / "out1.dat"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "out" / "out2.dat"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "out" / "out3.dat"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "proc1.dat"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "proc2.dat"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "metadata.json"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "300_300.json"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "302_302.json"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "300_305.json"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "401_402.json"));
    ASSERT_FALSE(fs::is_regular_file(result_path / "processed" / "278_288.json"));

    test::assert_file_equals(result_path / "out" / "out1.dat", "abcde12345", false);
    test::assert_file_equals(result_path / "out" / "out2.dat", "", false);
    test::assert_file_equals(result_path / "out" / "out3.dat", "X@?", false);
    test::assert_file_equals(result_path / "processed" / "proc1.dat", " !", false);
    test::assert_file_equals(result_path / "processed" / "proc2.dat", "Op%%b+", false);

    test::assert_file_equals(result_path / "processed" / "300_300.json",
                             "{\"page-faults\":{\"dummy0\":1,\"dummy10\":3,"
                             "\"dummy9\":2},"
                             "\"walltime\":[\"dummy4\",\"dummy5\",\"dummy6\","
                             "\"dummy7\"]}", true);
    test::assert_file_equals(result_path / "processed" / "302_302.json",
                             "{\"page-faults\":[],\"walltime\":[\"dummy11\"]}", true);
    test::assert_file_equals(result_path / "processed" / "300_305.json",
                             "{\"page-faults\":[\"dummy22\",\"dummy000\"],"
                             "\"walltime\":{}}", true);
    test::assert_file_equals(result_path / "processed" / "401_402.json",
                             "{\"walltime\": {}}", true);
    test::assert_file_equals(result_path / "processed" / "metadata.json",
                             CLIENT_METADATA1_EXPECTED, true);

    fs::remove_all(result_path);
  }
}

TEST_F(StdClientTest, StandardCommTestNoValidFiles) {
  for (int i = 0; i < CLIENT_TEST_REPEAT; i++) {
    const fs::path result_path("test_result_dir");
    const unsigned long long file_timeout_seconds = 124941;
    const std::string result_dir = result_path.filename();
    const std::string profiled_filename = "test_command123";
    const unsigned int buf_size = 1024;
    const int subclients = 5;

    int created_subclients = 0;

    nlohmann::json results[subclients];

    std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
      std::make_unique<test::MockSubclient::Factory>([&](test::MockSubclient &s) {
        std::string result_str = "{}";

        switch (created_subclients) {
        case 0:
          result_str =
            "{\"<SYSCALL_TREE>\": [12894, [\"300\", \"301\", \"302\", \"305\"], {"
            "\"300\": {\"parent\": null, \"tag\": "
            "[\"test_command\", \"300/300\", 0, 568]}, "
            "\"301\": {\"parent\": \"300\", \"tag\": "
            "[\"test_command\", \"300/301\", 585, 1004]}, "
            "\"302\": {\"parent\": \"300\", \"tag\": [\"sleep\", "
            "\"302/302\", 1002, 67]}, "
            "\"305\": {\"parent\": \"301\", \"tag\": "
            "[\"test_command\", \"300/305\", 1050, 1]}}]}";
          break;

        case 1:
          result_str =
            "{\"<SAMPLE>\": {"
            "\"300_300\": {\"first_time\": 12894, \"sampled_time\": 18284, "
            "\"offcpu_regions\": [[12895, 5], [13594, 999], [15894, 128]], "
            "\"walltime\": [\"dummy4\", \"dummy5\", \"dummy6\", \"dummy7\"], "
            "\"page-faults\": {\"dummy0\": 1, \"dummy9\": 2, \"dummy10\": 3}}}}";
          break;

        case 2:
          // Nothing
          break;

        case 3:
          result_str =
            "{\"<SYSCALL>\": {\"300\": [\"x\", \"y\", \"z\"], \"305\": "
            "[\"@\"], \"302\": [\"y\", \"*\"]}, "
            "\"<SAMPLE>\": {"
            "\"302_302\": {\"first_time\": 13000, \"sampled_time\": 100, "
            "\"offcpu_regions\": [], \"walltime\": [\"dummy11\"], "
            "\"page-faults\": []}, \"300_305\": {\"first_time\": 13001,"
            "\"sampled_time\": 585, \"offcpu_regions\": [[18753, 100]], "
            "\"walltime\": {}, "
            "\"page-faults\": [\"dummy22\", \"dummy000\"]}}}";
          break;

        case 4:
          // Nothing
          break;

        default:
          // Nothing
          break;
        }

        int index = created_subclients++;
        results[index] = nlohmann::json::parse(result_str);

        EXPECT_CALL(s, construct(_, profiled_filename, buf_size)).Times(1);
        EXPECT_CALL(s, real_process).Times(1);
        EXPECT_CALL(s, get_connection_instructions)
            .Times(1)
            .WillRepeatedly(Return(std::to_string(created_subclients)));
        EXPECT_CALL(s, get_result).Times(1).WillRepeatedly(ReturnRef(results[index]));
      }, true);

    std::unique_ptr<aperf::Connection> mock_connection =
      std::make_unique<StrictMock<test::MockConnection> >();

    test::MockConnection &connection = *((test::MockConnection *)mock_connection.get());

    EXPECT_CALL(connection, get_buf_size).Times(AtLeast(1)).WillRepeatedly(Return(buf_size));

    {
      InSequence sequence;
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(2)
        .WillOnce(Return("start" + std::to_string(subclients) + " " + result_dir))
        .WillOnce(Return(profiled_filename));
      EXPECT_CALL(connection, write("mock 1 2 3 4 5", true)).Times(1);
      EXPECT_CALL(connection, write("start_profile", true)).Times(1);
      EXPECT_CALL(connection, write("out_files", true)).Times(1);
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("some_garbage"));
      EXPECT_CALL(connection, write("error_wrong_file_format", true)).Times(1);
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("z invalid.dat"));
      EXPECT_CALL(connection, write("error_wrong_file_format", true)).Times(1);
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("1 p"));
      EXPECT_CALL(connection, write("error_wrong_file_format", true)).Times(1);
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("s1 p test.dat"));
      EXPECT_CALL(connection, write("error_wrong_file_format", true)).Times(1);
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("700@o test.dat"));
      EXPECT_CALL(connection, write("error_wrong_file_format", true)).Times(1);
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(connection, write("finished", true)).Times(1);
      EXPECT_CALL(connection, close).Times(1);
    }

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock may seg fault otherwise).
    {
      aperf::StdClient::Factory factory(subclient_factory);
      std::unique_ptr<aperf::Client> client = factory.make_client(mock_connection,
                                                                  file_timeout_seconds);
      client->process();
    }

    ASSERT_EQ(created_subclients, subclients);

    ASSERT_TRUE(fs::is_directory(result_path));
    ASSERT_TRUE(fs::is_directory(result_path / "out"));
    ASSERT_TRUE(fs::is_directory(result_path / "processed"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "metadata.json"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "300_300.json"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "302_302.json"));
    ASSERT_TRUE(fs::is_regular_file(result_path / "processed" / "300_305.json"));

    test::assert_file_equals(result_path / "processed" / "300_300.json",
                             "{\"page-faults\":{\"dummy0\":1,\"dummy10\":3,"
                             "\"dummy9\":2},"
                             "\"walltime\":[\"dummy4\",\"dummy5\",\"dummy6\","
                             "\"dummy7\"]}", true);
    test::assert_file_equals(result_path / "processed" / "302_302.json",
                             "{\"page-faults\":[],\"walltime\":[\"dummy11\"]}", true);
    test::assert_file_equals(result_path / "processed" / "300_305.json",
                             "{\"page-faults\":[\"dummy22\",\"dummy000\"],"
                             "\"walltime\":{}}", true);
    test::assert_file_equals(result_path / "processed" / "metadata.json",
                             CLIENT_METADATA2_EXPECTED, true);

    fs::remove_all(result_path);
  }
}

TEST_F(StdClientTest, InvalidCommTest1) {
  for (int i = 0; i < CLIENT_TEST_REPEAT; i++) {
    const unsigned long long file_timeout_seconds = 124941;
    int created_subclients = 0;

    std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
      std::make_unique<test::MockSubclient::Factory>([&](test::MockSubclient &s) {
        created_subclients++;
      }, true);

    std::unique_ptr<aperf::Connection> mock_connection =
      std::make_unique<StrictMock<test::MockConnection> >();

    test::MockConnection &connection = *((test::MockConnection *)mock_connection.get());

    {
      InSequence sequence;
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("some_garbage123"));
      EXPECT_CALL(connection, write("error_wrong_command", true)).Times(1);
      EXPECT_CALL(connection, close).Times(1);
    }

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock may seg fault otherwise).
    {
      aperf::StdClient::Factory factory(subclient_factory);
      std::unique_ptr<aperf::Client> client = factory.make_client(mock_connection,
                                                                  file_timeout_seconds);
      client->process();
    }

    ASSERT_EQ(created_subclients, 0);
  }
}

TEST_F(StdClientTest, InvalidCommTest2) {
  for (int i = 0; i < CLIENT_TEST_REPEAT; i++) {
    const unsigned long long file_timeout_seconds = 124941;
    int created_subclients = 0;

    std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
      std::make_unique<test::MockSubclient::Factory>([&](test::MockSubclient &s) {
        created_subclients++;
      }, true);

    std::unique_ptr<aperf::Connection> mock_connection =
      std::make_unique<StrictMock<test::MockConnection> >();

    test::MockConnection &connection = *((test::MockConnection *)mock_connection.get());

    {
      InSequence sequence;
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("start test"));
      EXPECT_CALL(connection, write("error_wrong_command", true)).Times(1);
      EXPECT_CALL(connection, close).Times(1);
    }

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock may seg fault otherwise).
    {
      aperf::StdClient::Factory factory(subclient_factory);
      std::unique_ptr<aperf::Client> client = factory.make_client(mock_connection,
                                                                  file_timeout_seconds);
      client->process();
    }

    ASSERT_EQ(created_subclients, 0);
  }
}

TEST_F(StdClientTest, InvalidCommTest3) {
  for (int i = 0; i < CLIENT_TEST_REPEAT; i++) {
    const unsigned long long file_timeout_seconds = 124941;
    int created_subclients = 0;

    std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
      std::make_unique<test::MockSubclient::Factory>([&](test::MockSubclient &s) {
        created_subclients++;
      }, true);

    std::unique_ptr<aperf::Connection> mock_connection =
      std::make_unique<StrictMock<test::MockConnection> >();

    test::MockConnection &connection = *((test::MockConnection *)mock_connection.get());

    {
      InSequence sequence;
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("start700! test"));
      EXPECT_CALL(connection, write("error_wrong_command", true)).Times(1);
      EXPECT_CALL(connection, close).Times(1);
    }

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock may seg fault otherwise).
    {
      aperf::StdClient::Factory factory(subclient_factory);
      std::unique_ptr<aperf::Client> client = factory.make_client(mock_connection,
                                                                  file_timeout_seconds);
      client->process();
    }

    ASSERT_EQ(created_subclients, 0);
  }
}

TEST_F(StdClientTest, InvalidCommTest4) {
  for (int i = 0; i < CLIENT_TEST_REPEAT; i++) {
    const unsigned long long file_timeout_seconds = 124941;
    int created_subclients = 0;

    std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
      std::make_unique<test::MockSubclient::Factory>([&](test::MockSubclient &s) {
        created_subclients++;
      }, true);

    std::unique_ptr<aperf::Connection> mock_connection =
      std::make_unique<StrictMock<test::MockConnection> >();

    test::MockConnection &connection = *((test::MockConnection *)mock_connection.get());

    {
      InSequence sequence;
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("start8 "));
      EXPECT_CALL(connection, write("error_wrong_command", true)).Times(1);
      EXPECT_CALL(connection, close).Times(1);
    }

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock may seg fault otherwise).
    {
      aperf::StdClient::Factory factory(subclient_factory);
      std::unique_ptr<aperf::Client> client = factory.make_client(mock_connection,
                                                                  file_timeout_seconds);
      client->process();
    }

    ASSERT_EQ(created_subclients, 0);
  }
}

TEST_F(StdClientTest, InvalidCommTest5) {
  for (int i = 0; i < CLIENT_TEST_REPEAT; i++) {
    const unsigned long long file_timeout_seconds = 124941;
    int created_subclients = 0;

    std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
      std::make_unique<test::MockSubclient::Factory>([&](test::MockSubclient &s) {
        created_subclients++;
      }, true);

    std::unique_ptr<aperf::Connection> mock_connection =
      std::make_unique<StrictMock<test::MockConnection> >();

    test::MockConnection &connection = *((test::MockConnection *)mock_connection.get());

    {
      InSequence sequence;
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("start1"));
      EXPECT_CALL(connection, write("error_wrong_command", true)).Times(1);
      EXPECT_CALL(connection, close).Times(1);
    }

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock may seg fault otherwise).
    {
      aperf::StdClient::Factory factory(subclient_factory);
      std::unique_ptr<aperf::Client> client = factory.make_client(mock_connection,
                                                                  file_timeout_seconds);
      client->process();
    }

    ASSERT_EQ(created_subclients, 0);
  }
}

TEST_F(StdClientTest, InvalidCommTest6) {
  for (int i = 0; i < CLIENT_TEST_REPEAT; i++) {
    const unsigned long long file_timeout_seconds = 124941;
    int created_subclients = 0;

    std::unique_ptr<aperf::Subclient::Factory> subclient_factory =
      std::make_unique<test::MockSubclient::Factory>([&](test::MockSubclient &s) {
        created_subclients++;
      }, true);

    std::unique_ptr<aperf::Connection> mock_connection =
      std::make_unique<StrictMock<test::MockConnection> >();

    test::MockConnection &connection = *((test::MockConnection *)mock_connection.get());

    {
      InSequence sequence;
      EXPECT_CALL(connection, read(NO_TIMEOUT))
        .Times(1)
        .WillOnce(Return("start0 test_result_dir"));
      EXPECT_CALL(connection, write("error_wrong_command", true)).Times(1);
      EXPECT_CALL(connection, close).Times(1);
    }

    // A separate scope is needed for ensuring the correct order
    // of destructor calls (gmock may seg fault otherwise).
    {
      aperf::StdClient::Factory factory(subclient_factory);
      std::unique_ptr<aperf::Client> client = factory.make_client(mock_connection,
                                                                  file_timeout_seconds);
      client->process();
    }

    ASSERT_EQ(created_subclients, 0);
  }
}
