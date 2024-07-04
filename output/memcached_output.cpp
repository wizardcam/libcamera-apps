#include "memcached_output.hpp"

MemcachedOutput::MemcachedOutput(VideoOptions const *options) : Output(options)
{
	opt = options;

	const char *config_string = "--SOCKET=\"/var/run/memcached/memcached.sock\" --BINARY-PROTOCOL";
	// const char *config_string = "--SERVER=localhost --BINARY-PROTOCOL";
	memc = memcached(config_string, strlen(config_string));
	if (memc == NULL)
		LOG_ERROR("Error connecting to memcached");
	
	// Test memcache
	const char *key = "my_key";
    const char *value = "Hello, Memcached!";
    size_t key_len = strlen(key);
	size_t value_length = strlen(value);
	
	rc = memcached_set(memc, key, key_len, value, value_length, 0, 0);

	// Retrieve the value from Memcached
	size_t returned_value_length;
	uint32_t returned_flags;
	char *returned_value = memcached_get(memc, key, strlen(key),
											&returned_value_length, &returned_flags, &rc);

	if (rc == MEMCACHED_SUCCESS) {
		// Assert that the retrieved value matches the expected value
		assert(returned_value_length == value_length);
		assert(std::memcmp(returned_value, value, value_length) == 0);

		std::cout << "Retrieved value from Memcached matches expected value" << std::endl;
		std::cout << "Value: " << returned_value << std::endl;

		// Free the memory allocated by libmemcached
		free(returned_value);
	} else {
		LOG_ERROR("Failed to retrieve value from Memcached: ");
	}
	
	// Test redis
	redis = redisConnect("127.0.0.1", 6379);
	if (redis == NULL || redis->err)
	{
		if (redis)
		{
			LOG_ERROR("Error redis");
		}
		else
		{
			LOG_ERROR("Can't allocate redis context");
		}
		exit(1);
	}
	// redisCommand(redis, "SET LibcameraService Alive");
}

MemcachedOutput::~MemcachedOutput()
{	
	memcached_free(memc);
	freeReplyObject(reply);
    redisFree(redis);
}

void MemcachedOutput::outputBuffer(void *mem, size_t size, int64_t /*timestamp_us*/ J, uint32_t /*flags*/)
{
	int64_t t =
		std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
			.count();
	char timestamp[16];
	sprintf(timestamp, "%li", t);
	// Flag set to 16 since the python bmemcached protocol library recognizes binary data with flag 16
	// This way the bmemcached library does not decode when reading.
	memcached_return_t rc = memcached_set(memc, timestamp, strlen(timestamp), (char *)mem, size,(time_t)0, (uint32_t)16);
	
	if (rc == MEMCACHED_SUCCESS)
	{
		LOG(2, "Value added successfully to memcached: " << timestamp);
	}
	else
		LOG_ERROR("Error: " << rc << " adding value to memcached " << timestamp);

	std::string time_str = timestamp;
	std::string redis_command = "XADD Libcamera MAXLEN ~ 1000 * ";
	redis_command += "event NewFrame ";
	redis_command += "memcached " + time_str + " ";
	redis_command += "sensor_id Libcamera ";

	// TODO read from actual joint which is set in redis
	redis_command += "joint_key -1 ";
	redis_command += "track_id -1 ";
	redis_command += "start_time -1 ";
	redis_command += "feedback_id -1 ";

	redis_command += "width " + std::to_string(opt->width) + " ";
	redis_command += "height " + std::to_string(opt->height) + " ";
	redis_command += "gain " + std::to_string(opt->gain) + " ";
	redis_command += "roi " + opt->roi;
	// TODO fix type conversions
	// redis_command += "framerate " + opt->framerate + " ";
	// redis_command += "shutter " + opt->shutter + " ";

	redisReply *reply = (redisReply *)redisCommand(redis, redis_command.c_str());

    if (reply == NULL) {
		LOG_ERROR("Failed to execute command");
    }

    if (reply->type == REDIS_REPLY_ERROR) {
		LOG_ERROR("Redis reply error");
    } else {
		LOG(2, "Entry added to redis");
    }
}
