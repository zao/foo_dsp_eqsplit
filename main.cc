#include <SDK/foobar2000.h>

#include <boost/archive/binary_oarchive.hpp>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/serialization/vector.hpp>
#include <iostream>
#include <optional>
#include <sstream>
#include <vector>

struct dsp_preset_helper
{
    dsp_preset_helper() = default;
    explicit dsp_preset_helper(dsp_preset const& p_preset)
    {
        t_size sz = p_preset.get_data_size();
        t_uint8 const* p = static_cast<t_uint8 const*>(p_preset.get_data());

        data.resize(sz);
        std::copy(p, p + sz, &data[0]);
        owner = p_preset.get_owner();
    }

    void get_preset(dsp_preset& p_preset)
    {
        p_preset.set_data(&data[0], data.size());
        p_preset.set_owner(owner);
    }

    template<class Archive>
    void serialize(Archive& p_ar, unsigned int const p_version)
    {
        p_ar& data;
        p_ar& owner;
    }

  private:
    std::vector<t_uint8> data;
    GUID owner{};
};

namespace boost {
namespace serialization {
template<class Archive>
void
serialize(Archive& p_ar, GUID& p_guid, unsigned int const file_version)
{
    p_ar& p_guid.Data1;
    p_ar& p_guid.Data2;
    p_ar& p_guid.Data3;
    p_ar& p_guid.Data4;
}
}
}

struct eqsplit_dsp : dsp_impl_base
{
    eqsplit_dsp(dsp_preset const& p_preset)
    {
        dsp_preset_impl preset[2];
        get_child_presets(p_preset, preset[0], preset[1]);

        for (t_size i = 0; i < 2; ++i) {
            service_ptr_t<dsp> ptr;
            dsp_entry::g_instantiate(ptr, preset[i]);
            ptr->service_query_t(eq[i]);
        }
    }

    static void get_child_presets(dsp_preset const& p_src, dsp_preset& p_dst0, dsp_preset& p_dst1)
    {
        char const* p = static_cast<char const*>(p_src.get_data());
        std::istringstream iss(std::string(p, p_src.get_data_size()));
        boost::archive::binary_iarchive ia(iss);

        dsp_preset_helper h;
        ia >> h;
        h.get_preset(p_dst0);
        ia >> h;
        h.get_preset(p_dst1);
    }

    static void set_child_presets(dsp_preset& p_src, dsp_preset const& p_dst0, dsp_preset const& p_dst1)
    {
        std::ostringstream oss;
        boost::archive::binary_oarchive oa(oss);

        oa << dsp_preset_helper(p_dst0);
        oa << dsp_preset_helper(p_dst1);

        std::string s = oss.str();
        p_src.set_data(reinterpret_cast<t_uint8 const*>(s.data()), s.size());
        p_src.set_owner(s_guid);
    }

    static std::optional<GUID> find_equalizer()
    {
        service_enum_t<dsp_entry> e;
        service_ptr_t<dsp_entry> ptr;
        pfc::string8 name;
        pfc::string8 eq_name = "Equalizer";
        std::optional<GUID> guid;
        while (e.next(ptr)) {
            ptr->get_name(name);
            if (name == eq_name) {
                guid = ptr->get_guid();
                break;
            }
        }
        return guid;
    }

    static void g_get_name(pfc::string_base& p_out) { p_out = "Split equalizer"; }
    static bool g_get_default_preset(dsp_preset& p_preset)
    {
        std::optional<GUID> guid = find_equalizer();
        if (guid) {
            dsp_preset_impl eq_preset;
            dsp_entry::g_get_default_preset(eq_preset, *guid);
            set_child_presets(p_preset, eq_preset, eq_preset);
        }
        return true;
    }

    static GUID g_get_guid() { return s_guid; }
    static bool g_have_config_popup() { return true; }

    struct split_config_callback : dsp_preset_edit_callback
    {
        virtual void on_preset_changed(dsp_preset const& p_new) { preset = p_new; }

        std::optional<dsp_preset_impl> preset;
    };

    static void g_show_config_popup(dsp_preset const& p_data, HWND p_parent, dsp_preset_edit_callback& p_callback)
    {
        dsp_preset_impl preset[2];
        get_child_presets(p_data, preset[0], preset[1]);

        split_config_callback cb[2];
        std::optional<GUID> guid = find_equalizer();
        if (guid) {
            service_ptr_t<dsp_entry> ptr;
            dsp_entry::g_get_interface(ptr, *guid);
            ptr->g_show_config_popup_v2(preset[0], p_parent, cb[0]);
            ptr->g_show_config_popup_v2(preset[1], p_parent, cb[1]);
        }
        if (cb[0].preset || cb[1].preset) {
            dsp_preset_impl ret;
            set_child_presets(ret, cb[0].preset ? *cb[0].preset : preset[0], cb[1].preset ? *cb[1].preset : preset[1]);
            p_callback.on_preset_changed(ret);
        }
    }

    virtual void on_endoftrack(abort_callback& p_abort) {}
    virtual void on_endofplayback(abort_callback& p_abort) {}

    struct debug_abort_callback : abort_callback
    {
        explicit debug_abort_callback(abort_callback& p_abort)
          : cb(p_abort)
        {
        }

        virtual bool is_aborting() const { return cb.is_aborting(); }

        //! Retrieves event object that can be used with some OS calls. The even object becomes signaled when abort is
        //! triggered. On win32, this is equivalent to win32 event handle (see: CreateEvent). \n You must not close this
        //! handle or call any methods that change this handle's state (SetEvent() or ResetEvent()), you can only wait
        //! for it.
        virtual abort_callback_event get_abort_event() const { return cb.get_abort_event(); }

      private:
        abort_callback& cb;
    };

    virtual bool on_chunk(audio_chunk* p_chunk, abort_callback& p_abort)
    {
        if (2 != p_chunk->get_channel_count())
            return true;

        dsp_chunk_list_impl cl[2];
        metadb_handle_ptr mh;
        get_cur_file(mh);

        split_channels(p_chunk, cl[0], cl[1]);
        for (t_size i = 0; i < 2; ++i) {
            debug_abort_callback dac(p_abort);
            eq[i]->run_v2(cl + i, mh, 0, dac);
        }

        if (cl[0].get_count() && cl[1].get_count()) {
            merge_channels(p_chunk, cl[0], cl[1]);
        } else {
            return false;
        }
        return true;
    }

    void split_channels(audio_chunk* p_chunk, dsp_chunk_list& cl0, dsp_chunk_list& cl1)
    {
        audio_chunk* chunk[] = { cl0.insert_item(0), cl1.insert_item(0) };
        t_size sample_count = p_chunk->get_sample_count();
        t_size sample_rate = p_chunk->get_sample_rate();

        for (t_size i = 0; i < 2; ++i) {
            chunk[i]->set_channels(1);
            chunk[i]->set_data_size(sample_count);
            chunk[i]->set_sample_count(sample_count);
            chunk[i]->set_sample_rate((unsigned int)sample_rate);

            audio_sample *dst = chunk[i]->get_data(), *src = p_chunk->get_data();
            for (t_size sample = 0; sample < sample_count; ++sample) {
                *dst++ = src[sample * 2 + i];
            }
        }
    }

    void merge_channels(audio_chunk* p_chunk, dsp_chunk_list& cl0, dsp_chunk_list& cl1)
    {
        audio_chunk* chunk[] = { cl0.get_item(0), cl1.get_item(0) };
        t_size n_items[] = { cl0.get_count(), cl1.get_count() };
        assert(cl0.get_count() == cl1.get_count());
        assert(chunk[0]->get_sample_count() == chunk[1]->get_sample_count());
        assert(chunk[0]->get_sample_rate() == chunk[1]->get_sample_rate());
        t_size sample_count = chunk[0]->get_sample_count();

        p_chunk->set_data_size(sample_count * 2);
        p_chunk->set_sample_count(sample_count);
        audio_sample *dst = p_chunk->get_data(), *src[] = { chunk[0]->get_data(), chunk[1]->get_data() };
        for (t_size i = 0; i < sample_count; ++i) {
            *dst++ = src[0][i];
            *dst++ = src[1][i];
        }
    }

    virtual void flush()
    {
        for (t_size i = 0; i < 2; ++i)
            eq[i]->flush();
    }

    virtual double get_latency() { return eq[0]->get_latency(); }
    virtual bool need_track_change_mark() { return eq[0]->need_track_change_mark(); }

  private:
    static GUID const s_guid;
    service_ptr_t<dsp_v2> eq[2];
};

// {29213FA7-160E-49b9-AAF0-9145A7F5F776}
GUID const eqsplit_dsp::s_guid = { 0x29213fa7, 0x160e, 0x49b9, { 0xaa, 0xf0, 0x91, 0x45, 0xa7, 0xf5, 0xf7, 0x76 } };

dsp_factory_t<eqsplit_dsp> g_asdf;

DECLARE_COMPONENT_VERSION("Split equalizer", "v0.0.2", "Zao")
