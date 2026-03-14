#include <spectre/nvim.h>
#include <cstdio>
#include <cstring>

namespace spectre {

bool NvimRpc::initialize(NvimProcess& process) {
    process_ = &process;
    read_buf_.resize(1024 * 1024);
    running_ = true;

    reader_thread_ = std::thread(&NvimRpc::reader_thread_func, this);
    return true;
}

void NvimRpc::shutdown() {
    running_ = false;
    if (reader_thread_.joinable()) {
        reader_thread_.join();
    }
}

MpackValue NvimRpc::request(const std::string& method, const std::vector<MpackValue>& params) {
    uint32_t msgid = next_msgid_++;

    char buf[65536];
    mpack_writer_t writer;
    mpack_writer_init(&writer, buf, sizeof(buf));

    mpack_start_array(&writer, 4);
    mpack_write_u32(&writer, 0);
    mpack_write_u32(&writer, msgid);
    mpack_write_str(&writer, method.c_str(), (uint32_t)method.size());

    mpack_start_array(&writer, (uint32_t)params.size());
    for (auto& p : params) {
        write_value(&writer, p);
    }
    mpack_finish_array(&writer);
    mpack_finish_array(&writer);

    size_t len = mpack_writer_buffer_used(&writer);
    mpack_writer_destroy(&writer);

    process_->write(reinterpret_cast<uint8_t*>(buf), len);

    std::unique_lock<std::mutex> lock(response_mutex_);
    response_cv_.wait(lock, [&]() { return responses_.count(msgid) > 0; });
    auto resp = std::move(responses_[msgid]);
    responses_.erase(msgid);
    return std::move(resp.result);
}

void NvimRpc::notify(const std::string& method, const std::vector<MpackValue>& params) {
    char buf[65536];
    mpack_writer_t writer;
    mpack_writer_init(&writer, buf, sizeof(buf));

    mpack_start_array(&writer, 3);
    mpack_write_u32(&writer, 2);
    mpack_write_str(&writer, method.c_str(), (uint32_t)method.size());

    mpack_start_array(&writer, (uint32_t)params.size());
    for (auto& p : params) {
        write_value(&writer, p);
    }
    mpack_finish_array(&writer);
    mpack_finish_array(&writer);

    size_t len = mpack_writer_buffer_used(&writer);
    mpack_writer_destroy(&writer);

    process_->write(reinterpret_cast<uint8_t*>(buf), len);
}

std::vector<RpcNotification> NvimRpc::drain_notifications() {
    std::lock_guard<std::mutex> lock(notif_mutex_);
    std::vector<RpcNotification> result;
    while (!notifications_.empty()) {
        result.push_back(std::move(notifications_.front()));
        notifications_.pop();
    }
    return result;
}

void NvimRpc::reader_thread_func() {
    std::vector<uint8_t> accum;
    accum.reserve(1024 * 1024);
    uint8_t chunk[65536];

    while (running_) {
        int n = process_->read(chunk, sizeof(chunk));
        if (n <= 0) {
            if (!running_) break;
            fprintf(stderr, "nvim pipe read error\n");
            break;
        }

        accum.insert(accum.end(), chunk, chunk + n);

        while (!accum.empty()) {
            size_t accum_before = accum.size();
            mpack_reader_t reader;
            mpack_reader_init_data(&reader, reinterpret_cast<char*>(accum.data()), accum.size());

            MpackValue msg = read_value(&reader);

            if (mpack_reader_error(&reader) != mpack_ok) {
                mpack_reader_destroy(&reader);
                break;
            }

            size_t remaining = mpack_reader_remaining(&reader, nullptr);
            size_t consumed = accum_before - remaining;
            mpack_reader_destroy(&reader);

            if (consumed == 0) break;

            accum.erase(accum.begin(), accum.begin() + consumed);

            if (msg.type == MpackValue::Array && msg.array_val.size() >= 3) {
                int type = (int)msg.array_val[0].as_int();

                if (type == 1 && msg.array_val.size() >= 4) {
                    RpcResponse resp;
                    resp.msgid = (uint32_t)msg.array_val[1].as_int();
                    resp.error = std::move(msg.array_val[2]);
                    resp.result = std::move(msg.array_val[3]);

                    std::lock_guard<std::mutex> lock(response_mutex_);
                    responses_[resp.msgid] = std::move(resp);
                    response_cv_.notify_all();
                } else if (type == 2 && msg.array_val.size() >= 3) {
                    RpcNotification notif;
                    notif.method = msg.array_val[1].as_str();
                    if (msg.array_val[2].type == MpackValue::Array) {
                        notif.params = std::move(msg.array_val[2].array_val);
                    }

                    std::lock_guard<std::mutex> lock(notif_mutex_);
                    notifications_.push(std::move(notif));
                }
            }
        }
    }
}

void NvimRpc::write_value(mpack_writer_t* writer, const MpackValue& val) {
    switch (val.type) {
    case MpackValue::Nil:    mpack_write_nil(writer); break;
    case MpackValue::Bool:   mpack_write_bool(writer, val.bool_val); break;
    case MpackValue::Int:    mpack_write_i64(writer, val.int_val); break;
    case MpackValue::UInt:   mpack_write_u64(writer, val.uint_val); break;
    case MpackValue::Float:  mpack_write_double(writer, val.float_val); break;
    case MpackValue::String:
        mpack_write_str(writer, val.str_val.c_str(), (uint32_t)val.str_val.size());
        break;
    case MpackValue::Array:
        mpack_start_array(writer, (uint32_t)val.array_val.size());
        for (auto& v : val.array_val) write_value(writer, v);
        mpack_finish_array(writer);
        break;
    case MpackValue::Map:
        mpack_start_map(writer, (uint32_t)val.map_val.size());
        for (auto& [k, v] : val.map_val) {
            write_value(writer, k);
            write_value(writer, v);
        }
        mpack_finish_map(writer);
        break;
    default: mpack_write_nil(writer); break;
    }
}

MpackValue NvimRpc::read_value(mpack_reader_t* reader) {
    MpackValue val;
    mpack_tag_t tag = mpack_read_tag(reader);

    if (mpack_reader_error(reader) != mpack_ok) {
        return val;
    }

    switch (mpack_tag_type(&tag)) {
    case mpack_type_nil:
        val.type = MpackValue::Nil;
        break;
    case mpack_type_bool:
        val.type = MpackValue::Bool;
        val.bool_val = mpack_tag_bool_value(&tag);
        break;
    case mpack_type_int:
        val.type = MpackValue::Int;
        val.int_val = mpack_tag_int_value(&tag);
        break;
    case mpack_type_uint:
        val.type = MpackValue::UInt;
        val.uint_val = mpack_tag_uint_value(&tag);
        break;
    case mpack_type_float:
        val.type = MpackValue::Float;
        val.float_val = mpack_tag_float_value(&tag);
        break;
    case mpack_type_double:
        val.type = MpackValue::Float;
        val.float_val = mpack_tag_double_value(&tag);
        break;
    case mpack_type_str: {
        uint32_t len = mpack_tag_str_length(&tag);
        val.type = MpackValue::String;
        val.str_val.resize(len);
        mpack_read_bytes(reader, val.str_val.data(), len);
        mpack_done_str(reader);
        break;
    }
    case mpack_type_bin: {
        uint32_t len = mpack_tag_bin_length(&tag);
        val.type = MpackValue::String;
        val.str_val.resize(len);
        mpack_read_bytes(reader, val.str_val.data(), len);
        mpack_done_bin(reader);
        break;
    }
    case mpack_type_ext: {
        uint32_t len = mpack_tag_ext_length(&tag);
        if (len <= 8) {
            char ext_data[8] = {};
            mpack_read_bytes(reader, ext_data, len);
            int64_t ext_val = 0;
            for (uint32_t i = 0; i < len; i++) {
                ext_val = (ext_val << 8) | (uint8_t)ext_data[i];
            }
            val.type = MpackValue::Int;
            val.int_val = ext_val;
        } else {
            mpack_skip_bytes(reader, len);
            val.type = MpackValue::Nil;
        }
        mpack_done_ext(reader);
        break;
    }
    case mpack_type_array: {
        uint32_t count = mpack_tag_array_count(&tag);
        val.type = MpackValue::Array;
        val.array_val.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            val.array_val.push_back(read_value(reader));
            if (mpack_reader_error(reader) != mpack_ok) return val;
        }
        mpack_done_array(reader);
        break;
    }
    case mpack_type_map: {
        uint32_t count = mpack_tag_map_count(&tag);
        val.type = MpackValue::Map;
        val.map_val.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            auto key = read_value(reader);
            auto value = read_value(reader);
            if (mpack_reader_error(reader) != mpack_ok) return val;
            val.map_val.emplace_back(std::move(key), std::move(value));
        }
        mpack_done_map(reader);
        break;
    }
    default:
        val.type = MpackValue::Nil;
        break;
    }

    return val;
}

MpackValue NvimRpc::make_int(int64_t v) {
    MpackValue val; val.type = MpackValue::Int; val.int_val = v; return val;
}
MpackValue NvimRpc::make_uint(uint64_t v) {
    MpackValue val; val.type = MpackValue::UInt; val.uint_val = v; return val;
}
MpackValue NvimRpc::make_str(const std::string& v) {
    MpackValue val; val.type = MpackValue::String; val.str_val = v; return val;
}
MpackValue NvimRpc::make_bool(bool v) {
    MpackValue val; val.type = MpackValue::Bool; val.bool_val = v; return val;
}
MpackValue NvimRpc::make_array(std::vector<MpackValue> v) {
    MpackValue val; val.type = MpackValue::Array; val.array_val = std::move(v); return val;
}
MpackValue NvimRpc::make_map(std::vector<std::pair<MpackValue, MpackValue>> v) {
    MpackValue val; val.type = MpackValue::Map; val.map_val = std::move(v); return val;
}
MpackValue NvimRpc::make_nil() {
    return MpackValue{};
}

} // namespace spectre
