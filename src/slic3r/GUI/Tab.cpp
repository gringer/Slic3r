// #include "libslic3r/GCodeSender.hpp"
#include "slic3r/Utils/Serial.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"
#include "PresetHints.hpp"
#include "libslic3r/Utils.hpp"

#include "slic3r/Utils/Http.hpp"
#include "slic3r/Utils/PrintHost.hpp"
#include "BonjourDialog.hpp"
#include "WipeTowerDialog.hpp"
#include "ButtonsDescription.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include "wxExtensions.hpp"
#include <wx/wupdlock.h>

#include <libslic3r/GCodeWriter.hpp>
#include <libslic3r/Slicing.hpp>

#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "ConfigWizard.hpp"

namespace Slic3r {
namespace GUI {


wxDEFINE_EVENT(EVT_TAB_VALUE_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_TAB_PRESETS_CHANGED, SimpleEvent);

Tab::Tab(wxNotebook* parent, const wxString& title, Preset::Type type) :
    m_parent(parent), m_title(title), m_type(type)
{
    Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL/*, name*/);
    this->SetFont(Slic3r::GUI::wxGetApp().normal_font());

    m_compatible_printers.type			= Preset::TYPE_PRINTER;
    m_compatible_printers.key_list		= "compatible_printers";
    m_compatible_printers.key_condition	= "compatible_printers_condition";
    m_compatible_printers.dialog_title 	= _(L("Compatible printers")).ToUTF8();
    m_compatible_printers.dialog_label 	= _(L("Select the printers this profile is compatible with.")).ToUTF8();

    m_compatible_prints.type			= Preset::TYPE_PRINT;
    m_compatible_prints.key_list 		= "compatible_prints";
    m_compatible_prints.key_condition	= "compatible_prints_condition";
    m_compatible_prints.dialog_title 	= _(L("Compatible print profiles")).ToUTF8();
    m_compatible_prints.dialog_label 	= _(L("Select the print profiles this profile is compatible with.")).ToUTF8();

    wxGetApp().tabs_list.push_back(this);

    m_em_unit = wxGetApp().em_unit();

    m_config_manipulation = get_config_manipulation();

    Bind(wxEVT_SIZE, ([this](wxSizeEvent &evt) {
        for (auto page : m_pages)
            if (! page.get()->IsShown())
                page->layout_valid = false;
        evt.Skip();
    }));
}

void Tab::set_type()
{
    if (m_name == "print")              { m_type = Slic3r::Preset::TYPE_PRINT; }
    else if (m_name == "sla_print")     { m_type = Slic3r::Preset::TYPE_SLA_PRINT; }
    else if (m_name == "filament")      { m_type = Slic3r::Preset::TYPE_FILAMENT; }
    else if (m_name == "sla_material")  { m_type = Slic3r::Preset::TYPE_SLA_MATERIAL; }
    else if (m_name == "printer")       { m_type = Slic3r::Preset::TYPE_PRINTER; }
    else                                { m_type = Slic3r::Preset::TYPE_INVALID; assert(false); }
}

// sub new
void Tab::create_preset_tab()
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_preset_bundle = wxGetApp().preset_bundle;

    // Vertical sizer to hold the choice menu and the rest of the page.
#ifdef __WXOSX__
    auto  *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->SetSizeHints(this);
    this->SetSizer(main_sizer);

    // Create additional panel to Fit() it from OnActivate()
    // It's needed for tooltip showing on OSX
    m_tmp_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBK_LEFT | wxTAB_TRAVERSAL);
    auto panel = m_tmp_panel;
    auto  sizer = new wxBoxSizer(wxVERTICAL);
    m_tmp_panel->SetSizer(sizer);
    m_tmp_panel->Layout();

    main_sizer->Add(m_tmp_panel, 1, wxEXPAND | wxALL, 0);
#else
    Tab *panel = this;
    auto  *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->SetSizeHints(panel);
    panel->SetSizer(sizer);
#endif //__WXOSX__

    // preset chooser
    m_presets_choice = new PresetBitmapComboBox(panel, wxSize(35 * m_em_unit, -1));

    auto color = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

    //buttons
    m_scaled_buttons.reserve(6);
    m_scaled_buttons.reserve(2);

    add_scaled_button(panel, &m_btn_save_preset, "save");
    add_scaled_button(panel, &m_btn_delete_preset, "cross");

    m_show_incompatible_presets = false;
    add_scaled_bitmap(this, m_bmp_show_incompatible_presets, "flag_red");
    add_scaled_bitmap(this, m_bmp_hide_incompatible_presets, "flag_green");

    add_scaled_button(panel, &m_btn_hide_incompatible_presets, m_bmp_hide_incompatible_presets.name());

    // TRN "Save current Settings"
    m_btn_save_preset->SetToolTip(from_u8((boost::format(_utf8(L("Save current %s"))) % m_title).str()));
    m_btn_delete_preset->SetToolTip(_(L("Delete this preset")));
    m_btn_delete_preset->Disable();

    add_scaled_button(panel, &m_question_btn, "question");

    m_question_btn->SetToolTip(_(L("Hover the cursor over buttons to find more information \n"
                                   "or click this button.")));

    // Determine the theme color of OS (dark or light)
    auto luma = wxGetApp().get_colour_approx_luma(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
    // Bitmaps to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    add_scaled_bitmap(this, m_bmp_value_lock  , luma >= 128 ? "lock_closed" : "lock_closed_white");
    add_scaled_bitmap(this, m_bmp_value_unlock, "lock_open");
    m_bmp_non_system = &m_bmp_white_bullet;
    // Bitmaps to be shown on the "Undo user changes" button next to each input field.
    add_scaled_bitmap(this, m_bmp_value_revert, "undo");
    add_scaled_bitmap(this, m_bmp_white_bullet, luma >= 128 ? "dot" : "dot_white");

    fill_icon_descriptions();
    set_tooltips_text();

    add_scaled_button(panel, &m_undo_btn,        m_bmp_white_bullet.name());
    add_scaled_button(panel, &m_undo_to_sys_btn, m_bmp_white_bullet.name());

    m_undo_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent) { on_roll_back_value(); }));
    m_undo_to_sys_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent) { on_roll_back_value(true); }));
    m_question_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent)
    {
        ButtonsDescription dlg(this, m_icon_descriptions);
        if (dlg.ShowModal() == wxID_OK) {
            // Colors for ui "decoration"
            for (Tab *tab : wxGetApp().tabs_list) {
                tab->m_sys_label_clr = wxGetApp().get_label_clr_sys();
                tab->m_modified_label_clr = wxGetApp().get_label_clr_modified();
                tab->update_labels_colour();
            }
        }
    }));

    // Colors for ui "decoration"
    m_sys_label_clr			= wxGetApp().get_label_clr_sys();
    m_modified_label_clr	= wxGetApp().get_label_clr_modified();
    m_default_text_clr		= wxGetApp().get_label_clr_default();

    // Sizer with buttons for mode changing
    m_mode_sizer = new ModeSizer(panel);

    const float scale_factor = /*wxGetApp().*/em_unit(this)*0.1;// GetContentScaleFactor();
    m_hsizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_hsizer, 0, wxEXPAND | wxBOTTOM, 3);
    m_hsizer->Add(m_presets_choice, 0, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
    m_hsizer->AddSpacer(int(4*scale_factor));
    m_hsizer->Add(m_btn_save_preset, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(4 * scale_factor));
    m_hsizer->Add(m_btn_delete_preset, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(16 * scale_factor));
    m_hsizer->Add(m_btn_hide_incompatible_presets, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(64 * scale_factor));
    m_hsizer->Add(m_undo_to_sys_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->Add(m_undo_btn, 0, wxALIGN_CENTER_VERTICAL);
    m_hsizer->AddSpacer(int(32 * scale_factor));
    m_hsizer->Add(m_question_btn, 0, wxALIGN_CENTER_VERTICAL);
    // m_hsizer->AddStretchSpacer(32);
    // StretchSpacer has a strange behavior under OSX, so
    // There is used just additional sizer for m_mode_sizer with right alignment
    auto mode_sizer = new wxBoxSizer(wxVERTICAL);
    mode_sizer->Add(m_mode_sizer, 1, wxALIGN_RIGHT);
    m_hsizer->Add(mode_sizer, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, wxOSX ? 15 : 10);

    //Horizontal sizer to hold the tree and the selected page.
    m_hsizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_hsizer, 1, wxEXPAND, 0);

    //left vertical sizer
    m_left_sizer = new wxBoxSizer(wxVERTICAL);
    m_hsizer->Add(m_left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);

    // tree
    m_treectrl = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(20 * m_em_unit, -1),
        wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
    m_left_sizer->Add(m_treectrl, 1, wxEXPAND);
    const int img_sz = int(16 * scale_factor + 0.5f);
    m_icons = new wxImageList(img_sz, img_sz, true, 1);
    // Index of the last icon inserted into $self->{icons}.
    m_icon_count = -1;
    m_treectrl->AssignImageList(m_icons);
    m_treectrl->AddRoot("root");
    m_treectrl->SetIndent(0);
    m_disable_tree_sel_changed_event = 0;

    m_treectrl->Bind(wxEVT_TREE_SEL_CHANGED, &Tab::OnTreeSelChange, this);
    m_treectrl->Bind(wxEVT_KEY_DOWN, &Tab::OnKeyDown, this);

    m_presets_choice->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent e) {
        //! Because of The MSW and GTK version of wxBitmapComboBox derived from wxComboBox, 
        //! but the OSX version derived from wxOwnerDrawnCombo, instead of:
        //! select_preset(m_presets_choice->GetStringSelection().ToUTF8().data()); 
        //! we doing next:
        int selected_item = m_presets_choice->GetSelection();
        if (m_selected_preset_item == size_t(selected_item) && !m_presets->current_is_dirty())
            return;
        if (selected_item >= 0) {
            std::string selected_string = m_presets_choice->GetString(selected_item).ToUTF8().data();
            if (selected_string.find(PresetCollection::separator_head()) == 0
                /*selected_string == "------- System presets -------" ||
                selected_string == "-------  User presets  -------"*/) {
                m_presets_choice->SetSelection(m_selected_preset_item);
                if (wxString::FromUTF8(selected_string.c_str()) == PresetCollection::separator(L("Add a new printer")))
                    wxTheApp->CallAfter([]() { wxGetApp().run_wizard(ConfigWizard::RR_USER); });
                return;
            }
            m_selected_preset_item = selected_item;
            select_preset(selected_string);
        }
    }));

    m_btn_save_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { save_preset(); }));
    m_btn_delete_preset->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) { delete_preset(); }));
    m_btn_hide_incompatible_presets->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e) {
        toggle_show_hide_incompatible();
    }));

    // Fill cache for mode bitmaps
    m_mode_bitmap_cache.reserve(3);
    m_mode_bitmap_cache.push_back(ScalableBitmap(this, "mode_simple"  , mode_icon_px_size()));
    m_mode_bitmap_cache.push_back(ScalableBitmap(this, "mode_advanced", mode_icon_px_size()));
    m_mode_bitmap_cache.push_back(ScalableBitmap(this, "mode_expert"  , mode_icon_px_size()));

    // Initialize the DynamicPrintConfig by default keys/values.
    build();
    rebuild_page_tree();
    m_completed = true;
}

void Tab::add_scaled_button(wxWindow* parent,
                            ScalableButton** btn,
                            const std::string& icon_name,
                            const wxString& label/* = wxEmptyString*/,
                            long style /*= wxBU_EXACTFIT | wxNO_BORDER*/)
{
    *btn = new ScalableButton(parent, wxID_ANY, icon_name, label, wxDefaultSize, wxDefaultPosition, style);
    m_scaled_buttons.push_back(*btn);
}

void Tab::add_scaled_bitmap(wxWindow* parent,
                            ScalableBitmap& bmp,
                            const std::string& icon_name)
{
    bmp = ScalableBitmap(parent, icon_name);
    m_scaled_bitmaps.push_back(&bmp);
}

void Tab::load_initial_data()
{
    m_config = &m_presets->get_edited_preset().config;
    bool has_parent = m_presets->get_selected_preset_parent() != nullptr;
    m_bmp_non_system = has_parent ? &m_bmp_value_unlock : &m_bmp_white_bullet;
    m_ttg_non_system = has_parent ? &m_ttg_value_unlock : &m_ttg_white_bullet_ns;
    m_tt_non_system  = has_parent ? &m_tt_value_unlock  : &m_ttg_white_bullet_ns;
}

Slic3r::GUI::PageShp Tab::add_options_page(const wxString& title, const std::string& icon, bool is_extruder_pages /*= false*/)
{
    // Index of icon in an icon list $self->{icons}.
    auto icon_idx = 0;
    if (!icon.empty()) {
        icon_idx = (m_icon_index.find(icon) == m_icon_index.end()) ? -1 : m_icon_index.at(icon);
        if (icon_idx == -1) {
            // Add a new icon to the icon list.
            m_scaled_icons_list.push_back(ScalableBitmap(this, icon));
            m_icons->Add(m_scaled_icons_list.back().bmp());
            icon_idx = ++m_icon_count;
            m_icon_index[icon] = icon_idx;
        }
    }
    // Initialize the page.
#ifdef __WXOSX__
    auto panel = m_tmp_panel;
#else
    auto panel = this;
#endif
    PageShp page(new Page(panel, title, icon_idx, m_mode_bitmap_cache));
//	page->SetBackgroundStyle(wxBG_STYLE_SYSTEM);
#ifdef __WINDOWS__
//	page->SetDoubleBuffered(true);
#endif //__WINDOWS__

    page->SetScrollbars(1, 20, 1, 2);
    page->Hide();
    m_hsizer->Add(page.get(), 1, wxEXPAND | wxLEFT, 5);

    if (!is_extruder_pages)
        m_pages.push_back(page);

    page->set_config(m_config);
    return page;
}

void Tab::OnActivate()
{
#ifdef __WXOSX__
    wxWindowUpdateLocker noUpdates(this);

    auto size = GetSizer()->GetSize();
    m_tmp_panel->GetSizer()->SetMinSize(size.x + m_size_move, size.y);
    Fit();
    m_size_move *= -1;
#endif // __WXOSX__
}

void Tab::update_labels_colour()
{
//	Freeze();
    //update options "decoration"
    for (const std::pair<std::string, int> &opt : m_options_list)
    {
        const wxColour *color = &m_sys_label_clr;

        // value isn't equal to system value
        if ((opt.second & osSystemValue) == 0) {
            // value is equal to last saved
            if ((opt.second & osInitValue) != 0)
                color = &m_default_text_clr;
            // value is modified
            else
                color = &m_modified_label_clr;
        }
        if (opt.first == "bed_shape" || opt.first == "compatible_prints" || opt.first == "compatible_printers") {
            wxStaticText* label = (m_colored_Labels.find(opt.first) == m_colored_Labels.end()) ? nullptr : m_colored_Labels.at(opt.first);
            if (label) {
                label->SetForegroundColour(*color);
                label->Refresh(true);
            }
            continue;
        }

        Field* field = get_field(opt.first);
        if (field == nullptr) continue;
        field->set_label_colour_force(color);
    }
//	Thaw();

    auto cur_item = m_treectrl->GetFirstVisibleItem();
    if (!cur_item || !m_treectrl->IsVisible(cur_item))
        return;
    while (cur_item) {
        auto title = m_treectrl->GetItemText(cur_item);
        for (auto page : m_pages)
        {
            if (page->title() != title)
                continue;

            const wxColor *clr = !page->m_is_nonsys_values ? &m_sys_label_clr :
                page->m_is_modified_values ? &m_modified_label_clr :
                &m_default_text_clr;

            m_treectrl->SetItemTextColour(cur_item, *clr);
            break;
        }
        cur_item = m_treectrl->GetNextVisible(cur_item);
    }
}

// Update UI according to changes
void Tab::update_changed_ui()
{
    if (m_postpone_update_ui)
        return;

    const bool deep_compare = (m_type == Slic3r::Preset::TYPE_PRINTER || m_type == Slic3r::Preset::TYPE_SLA_MATERIAL);
    auto dirty_options = m_presets->current_dirty_options(deep_compare);
    auto nonsys_options = m_presets->current_different_from_parent_options(deep_compare);
    if (m_type == Slic3r::Preset::TYPE_PRINTER) {
        TabPrinter* tab = static_cast<TabPrinter*>(this);
        if (tab->m_initial_extruders_count != tab->m_extruders_count)
            dirty_options.emplace_back("extruders_count");
        if (tab->m_sys_extruders_count != tab->m_extruders_count)
            nonsys_options.emplace_back("extruders_count");
        if (tab->m_initial_milling_count != tab->m_milling_count)
            dirty_options.emplace_back("milling_count");
        if (tab->m_sys_milling_count != tab->m_milling_count)
            nonsys_options.emplace_back("milling_count");
    }

    for (auto& it : m_options_list)
        it.second = m_opt_status_value;

    for (auto opt_key : dirty_options)	m_options_list[opt_key] &= ~osInitValue;
    for (auto opt_key : nonsys_options)	m_options_list[opt_key] &= ~osSystemValue;

//	Freeze();
    //update options "decoration"
    for (const auto opt : m_options_list)
    {
        bool is_nonsys_value = false;
        bool is_modified_value = true;
        const ScalableBitmap *sys_icon =	&m_bmp_value_lock;
        const ScalableBitmap *icon =		&m_bmp_value_revert;

        const wxColour *color =		m_is_default_preset ? &m_default_text_clr : &m_sys_label_clr;

        const wxString *sys_tt =	&m_tt_value_lock;
        const wxString *tt =		&m_tt_value_revert;

        // value isn't equal to system value
        if ((opt.second & osSystemValue) == 0) {
            is_nonsys_value = true;
            sys_icon = m_bmp_non_system;
            sys_tt = m_tt_non_system;
            // value is equal to last saved
            if ((opt.second & osInitValue) != 0)
                color = &m_default_text_clr;
            // value is modified
            else
                color = &m_modified_label_clr;
        }
        if ((opt.second & osInitValue) != 0)
        {
            is_modified_value = false;
            icon = &m_bmp_white_bullet;
            tt = &m_tt_white_bullet;
        }
        if (opt.first == "bed_shape" || opt.first == "compatible_prints" || opt.first == "compatible_printers") {
            wxStaticText* label = (m_colored_Labels.find(opt.first) == m_colored_Labels.end()) ? nullptr : m_colored_Labels.at(opt.first);
            if (label) {
                label->SetForegroundColour(*color);
                label->Refresh(true);
            }
            continue;
        }

        Field* field = get_field(opt.first);
        if (field == nullptr) continue;
        field->m_is_nonsys_value = is_nonsys_value;
        field->m_is_modified_value = is_modified_value;
        field->set_undo_bitmap(icon);
        field->set_undo_to_sys_bitmap(sys_icon);
        field->set_undo_tooltip(tt);
        field->set_undo_to_sys_tooltip(sys_tt);
        field->set_label_colour(color);
    }
//	Thaw();

    wxTheApp->CallAfter([this]() {
        if (parent()) //To avoid a crash, parent should be exist for a moment of a tree updating
            update_changed_tree_ui();
    });
}

void Tab::init_options_list()
{
    if (!m_options_list.empty())
        m_options_list.clear();

    for (const auto opt_key : m_config->keys())
        m_options_list.emplace(opt_key, m_opt_status_value);
}

template<class T>
void add_correct_opts_to_options_list(const std::string &opt_key, std::map<std::string, int>& map, Tab *tab, const int& value)
{
    T *opt_cur = static_cast<T*>(tab->m_config->option(opt_key));
    for (size_t i = 0; i < opt_cur->values.size(); i++)
        map.emplace(opt_key + "#" + std::to_string(i), value);
}

void TabPrinter::init_options_list()
{
    if (!m_options_list.empty())
        m_options_list.clear();

    for (const auto opt_key : m_config->keys())
    {
        if (opt_key == "bed_shape") {
            m_options_list.emplace(opt_key, m_opt_status_value);
            continue;
        }
        switch (m_config->option(opt_key)->type())
        {
        case coInts:	add_correct_opts_to_options_list<ConfigOptionInts		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coBools:	add_correct_opts_to_options_list<ConfigOptionBools		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coFloats:	add_correct_opts_to_options_list<ConfigOptionFloats		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coStrings:	add_correct_opts_to_options_list<ConfigOptionStrings	>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coPercents:add_correct_opts_to_options_list<ConfigOptionPercents	>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coPoints:	add_correct_opts_to_options_list<ConfigOptionPoints		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        default:		m_options_list.emplace(opt_key, m_opt_status_value);		break;
        }
    }
    m_options_list.emplace("extruders_count", m_opt_status_value);
}

void TabPrinter::msw_rescale()
{
    Tab::msw_rescale();

    // rescale missed options_groups
    const std::vector<PageShp>& pages = m_printer_technology == ptFFF ? m_pages_sla : m_pages_fff;
    for (auto page : pages)
        page->msw_rescale();

    Layout();
}

void TabSLAMaterial::init_options_list()
{
    if (!m_options_list.empty())
        m_options_list.clear();

    for (const auto opt_key : m_config->keys())
    {
        if (opt_key == "compatible_prints" || opt_key == "compatible_printers") {
            m_options_list.emplace(opt_key, m_opt_status_value);
            continue;
        }
        switch (m_config->option(opt_key)->type())
        {
        case coInts:	add_correct_opts_to_options_list<ConfigOptionInts		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coBools:	add_correct_opts_to_options_list<ConfigOptionBools		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coFloats:	add_correct_opts_to_options_list<ConfigOptionFloats		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coStrings:	add_correct_opts_to_options_list<ConfigOptionStrings	>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coPercents:add_correct_opts_to_options_list<ConfigOptionPercents	>(opt_key, m_options_list, this, m_opt_status_value);	break;
        case coPoints:	add_correct_opts_to_options_list<ConfigOptionPoints		>(opt_key, m_options_list, this, m_opt_status_value);	break;
        default:		m_options_list.emplace(opt_key, m_opt_status_value);		break;
        }
    }
}

void Tab::get_sys_and_mod_flags(const std::string& opt_key, bool& sys_page, bool& modified_page)
{
    auto opt = m_options_list.find(opt_key);
    if (opt == m_options_list.end()) 
        return;

    if (sys_page) sys_page = (opt->second & osSystemValue) != 0;
    modified_page |= (opt->second & osInitValue) == 0;
}

void Tab::update_changed_tree_ui()
{
    if (m_options_list.empty())
        return;
    auto cur_item = m_treectrl->GetFirstVisibleItem();
    if (!cur_item || !m_treectrl->IsVisible(cur_item))
        return;

    auto selected_item = m_treectrl->GetSelection();
    auto selection = selected_item ? m_treectrl->GetItemText(selected_item) : "";

    while (cur_item) {
        auto title = m_treectrl->GetItemText(cur_item);
        for (auto page : m_pages)
        {
            if (page->title() != title)
                continue;
            bool sys_page = true;
            bool modified_page = false;
            if (title == _("General")) {
                std::initializer_list<const char*> optional_keys{ "extruders_count", "bed_shape" };
                for (auto &opt_key : optional_keys) {
                    get_sys_and_mod_flags(opt_key, sys_page, modified_page);
                }
            }
            if (title == _("Dependencies")) {
                if (m_type == Slic3r::Preset::TYPE_PRINTER) {
                    sys_page = m_presets->get_selected_preset_parent() != nullptr;
                    modified_page = false;
                } else {
                    if (m_type == Slic3r::Preset::TYPE_FILAMENT || m_type == Slic3r::Preset::TYPE_SLA_MATERIAL)
                        get_sys_and_mod_flags("compatible_prints", sys_page, modified_page);
                    get_sys_and_mod_flags("compatible_printers", sys_page, modified_page);
                }
            }
            for (auto group : page->m_optgroups)
            {
                if (!sys_page && modified_page)
                    break;
                for (t_opt_map::iterator it = group->m_opt_map.begin(); it != group->m_opt_map.end(); ++it) {
                    const std::string& opt_key = it->first;
                    get_sys_and_mod_flags(opt_key, sys_page, modified_page);
                }
            }

            const wxColor *clr = sys_page		?	(m_is_default_preset ? &m_default_text_clr : &m_sys_label_clr) :
                                 modified_page	?	&m_modified_label_clr :
                                                    &m_default_text_clr;

            if (page->set_item_colour(clr))
                m_treectrl->SetItemTextColour(cur_item, *clr);

            page->m_is_nonsys_values = !sys_page;
            page->m_is_modified_values = modified_page;

            if (selection == title) {
                m_is_nonsys_values = page->m_is_nonsys_values;
                m_is_modified_values = page->m_is_modified_values;
            }
            break;
        }
        auto next_item = m_treectrl->GetNextVisible(cur_item);
        cur_item = next_item;
    }
    update_undo_buttons();
}

void Tab::update_undo_buttons()
{
    m_undo_btn->        SetBitmap_(m_is_modified_values ? m_bmp_value_revert: m_bmp_white_bullet);
    m_undo_to_sys_btn-> SetBitmap_(m_is_nonsys_values   ? *m_bmp_non_system : m_bmp_value_lock);

    m_undo_btn->SetToolTip(m_is_modified_values ? m_ttg_value_revert : m_ttg_white_bullet);
    m_undo_to_sys_btn->SetToolTip(m_is_nonsys_values ? *m_ttg_non_system : m_ttg_value_lock);
}

void Tab::on_roll_back_value(const bool to_sys /*= true*/)
{
    int os;
    if (to_sys)	{
        if (!m_is_nonsys_values) return;
        os = osSystemValue;
    }
    else {
        if (!m_is_modified_values) return;
        os = osInitValue;
    }

    m_postpone_update_ui = true;

    auto selection = m_treectrl->GetItemText(m_treectrl->GetSelection());
    for (auto page : m_pages)
        if (page->title() == selection)	{
            for (auto group : page->m_optgroups) {
                if (group->title == _("Capabilities")) {
                    if ((m_options_list["extruders_count"] & os) == 0)
                        to_sys ? group->back_to_sys_value("extruders_count") : group->back_to_initial_value("extruders_count");
                }
                if (group->title == _("Size and coordinates")) {
                    if ((m_options_list["bed_shape"] & os) == 0) {
                        to_sys ? group->back_to_sys_value("bed_shape") : group->back_to_initial_value("bed_shape");
                        load_key_value("bed_shape", true/*some value*/, true);
                    }

                }
                if (group->title == _("Profile dependencies")) {
                    // "compatible_printers" option doesn't exists in Printer Settimgs Tab
                    if (m_type != Preset::TYPE_PRINTER && (m_options_list["compatible_printers"] & os) == 0) {
                        to_sys ? group->back_to_sys_value("compatible_printers") : group->back_to_initial_value("compatible_printers");
                        load_key_value("compatible_printers", true/*some value*/, true);

                        bool is_empty = m_config->option<ConfigOptionStrings>("compatible_printers")->values.empty();
                        m_compatible_printers.checkbox->SetValue(is_empty);
                        is_empty ? m_compatible_printers.btn->Disable() : m_compatible_printers.btn->Enable();
                    }
                    // "compatible_prints" option exists only in Filament Settimgs and Materials Tabs
                    if ((m_type == Preset::TYPE_FILAMENT || m_type == Preset::TYPE_SLA_MATERIAL) && (m_options_list["compatible_prints"] & os) == 0) {
                        to_sys ? group->back_to_sys_value("compatible_prints") : group->back_to_initial_value("compatible_prints");
                        load_key_value("compatible_prints", true/*some value*/, true);

                        bool is_empty = m_config->option<ConfigOptionStrings>("compatible_prints")->values.empty();
                        m_compatible_prints.checkbox->SetValue(is_empty);
                        is_empty ? m_compatible_prints.btn->Disable() : m_compatible_prints.btn->Enable();
                    }
                }
                for (auto kvp : group->m_opt_map) {
                    const std::string& opt_key = kvp.first;
                    if ((m_options_list[opt_key] & os) == 0)
                        to_sys ? group->back_to_sys_value(opt_key) : group->back_to_initial_value(opt_key);
                }
            }
            break;
        }

    m_postpone_update_ui = false;
    update_changed_ui();
}

// Update the combo box label of the selected preset based on its "dirty" state,
// comparing the selected preset config with $self->{config}.
void Tab::update_dirty()
{
    m_presets->update_dirty_ui(m_presets_choice);
    on_presets_changed();
    update_changed_ui();
}

void Tab::update_tab_ui()
{
    m_selected_preset_item = m_presets->update_tab_ui(m_presets_choice, m_show_incompatible_presets, m_em_unit);
}

// Load a provied DynamicConfig into the tab, modifying the active preset.
// This could be used for example by setting a Wipe Tower position by interactive manipulation in the 3D view.
void Tab::load_config(const DynamicPrintConfig& config)
{
    bool modified = 0;
    for (auto opt_key : m_config->diff(config)) {
        m_config->set_key_value(opt_key, config.option(opt_key)->clone());
        modified = 1;
    }
    if (modified) {
        update_dirty();
        //# Initialize UI components with the config values.
        reload_config();
        update();
    }
}

// Reload current $self->{config} (aka $self->{presets}->edited_preset->config) into the UI fields.
void Tab::reload_config()
{
//	Freeze();
    for (auto page : m_pages)
        page->reload_config();
// 	Thaw();
}

void Tab::update_mode()
{
    m_mode = wxGetApp().get_mode();

    // update mode for ModeSizer
    m_mode_sizer->SetMode(m_mode);

    update_visibility();

    update_changed_tree_ui();
}

void Tab::update_visibility()
{
    Freeze(); // There is needed Freeze/Thaw to avoid a flashing after Show/Layout

    // m_detach_preset_btn will be shown always after call page->update_visibility()
    // So let save a "show state" of m_detach_preset_btn before update_visibility
    bool was_shown = false;
    if (m_detach_preset_btn)
        m_detach_preset_btn->IsShown();

    for (auto page : m_pages)
        page->update_visibility(m_mode);
    update_page_tree_visibility();

    // update visibility for detach_preset_btn
    if (m_detach_preset_btn)
        m_detach_preset_btn->Show(was_shown);

    Layout();
    Thaw();
}

void Tab::msw_rescale()
{
    m_em_unit = wxGetApp().em_unit();

    m_mode_sizer->msw_rescale();

    m_presets_choice->SetSize(35 * m_em_unit, -1);
    m_treectrl->SetMinSize(wxSize(20 * m_em_unit, -1));

    update_tab_ui();

    // rescale buttons and cached bitmaps
    for (const auto btn : m_scaled_buttons)
        btn->msw_rescale();
    for (const auto bmp : m_scaled_bitmaps)
        bmp->msw_rescale();
    for (ScalableBitmap& bmp : m_mode_bitmap_cache)
        bmp.msw_rescale();

    // rescale icons for tree_ctrl
    for (ScalableBitmap& bmp : m_scaled_icons_list)
        bmp.msw_rescale();
    // recreate and set new ImageList for tree_ctrl
    m_icons->RemoveAll();
    m_icons = new wxImageList(m_scaled_icons_list.front().bmp().GetWidth(), m_scaled_icons_list.front().bmp().GetHeight());
    for (ScalableBitmap& bmp : m_scaled_icons_list)
        m_icons->Add(bmp.bmp());
    m_treectrl->AssignImageList(m_icons);

    // rescale options_groups
    for (auto page : m_pages)
        page->msw_rescale();

    Layout();
}

Field* Tab::get_field(const t_config_option_key& opt_key, int opt_index/* = -1*/) const
{
    Field* field = nullptr;
    for (auto page : m_pages) {
        field = page->get_field(opt_key, opt_index);
        if (field != nullptr)
            return field;
    }
    return field;
}

// Set a key/value pair on this page. Return true if the value has been modified.
// Currently used for distributing extruders_count over preset pages of Slic3r::GUI::Tab::Printer
// after a preset is loaded.
bool Tab::set_value(const t_config_option_key& opt_key, const boost::any& value) {
    bool changed = false;
    for(auto page: m_pages) {
        if (page->set_value(opt_key, value))
        changed = true;
    }
    return changed;
}

// To be called by custom widgets, load a value into a config,
// update the preset selection boxes (the dirty flags)
// If value is saved before calling this function, put saved_value = true,
// and value can be some random value because in this case it will not been used
void Tab::load_key_value(const std::string& opt_key, const boost::any& value, bool saved_value /*= false*/)
{
    if (!saved_value) change_opt_value(*m_config, opt_key, value);
    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    if (opt_key == "compatible_printers" || opt_key == "compatible_prints") {
        // Don't select another profile if this profile happens to become incompatible.
        m_preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
    }
    m_presets->update_dirty_ui(m_presets_choice);
    on_presets_changed();
    update();
}

static wxString support_combo_value_for_config(const DynamicPrintConfig &config, bool is_fff)
{
    const std::string support         = is_fff ? "support_material"                 : "supports_enable";
    const std::string buildplate_only = is_fff ? "support_material_buildplate_only" : "support_buildplate_only";
    return
        ! config.opt_bool(support) ?
            _("None") :
            (is_fff && !config.opt_bool("support_material_auto")) ?
                _("For support enforcers only") :
                (config.opt_bool(buildplate_only) ? _("Support on build plate only") :
                                                    _("Everywhere"));
}

static wxString pad_combo_value_for_config(const DynamicPrintConfig &config)
{
    return config.opt_bool("pad_enable") ? (config.opt_bool("pad_around_object") ? _("Around object") : _("Below object")) : _("None");
}

void Tab::on_value_change(const std::string& opt_key, const boost::any& value)
{
    if (wxGetApp().plater() == nullptr) {
        return;
    }

    const bool is_fff = supports_printer_technology(ptFFF);
    ConfigOptionsGroup* og_freq_chng_params = wxGetApp().sidebar().og_freq_chng_params(is_fff);
    if (opt_key == "fill_density" || opt_key == "pad_enable")
    {
        boost::any val = og_freq_chng_params->get_config_value(*m_config, opt_key);
        og_freq_chng_params->set_value(opt_key, val);
    }
    
    if (opt_key == "pad_around_object") {
        for (PageShp &pg : m_pages) {
            Field * fld = pg->get_field(opt_key);
            if (fld) fld->set_value(value, false);
        }
    }

    if (is_fff ?
            (opt_key == "support_material" || opt_key == "support_material_auto" || opt_key == "support_material_buildplate_only") :
            (opt_key == "supports_enable"  || opt_key == "support_buildplate_only"))
        og_freq_chng_params->set_value("support", support_combo_value_for_config(*m_config, is_fff));

    if (! is_fff && (opt_key == "pad_enable" || opt_key == "pad_around_object"))
        og_freq_chng_params->set_value("pad", pad_combo_value_for_config(*m_config));

    if (opt_key == "brim_width")
    {
        bool val = m_config->opt_float("brim_width") > 0.0 || m_config->opt_float("brim_width_interior");
        og_freq_chng_params->set_value("brim", val);
    }

    if (opt_key == "wipe_tower" || opt_key == "single_extruder_multi_material" || opt_key == "extruders_count" )
        update_wiping_button_visibility();

    if (opt_key == "extruders_count")
        wxGetApp().plater()->on_extruders_change(boost::any_cast<int>(value));

    update();
}

// Show/hide the 'purging volumes' button
void Tab::update_wiping_button_visibility() {
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME
    bool wipe_tower_enabled = dynamic_cast<ConfigOptionBool*>(  (m_preset_bundle->prints.get_edited_preset().config  ).option("wipe_tower"))->value;
    bool multiple_extruders = dynamic_cast<ConfigOptionFloats*>((m_preset_bundle->printers.get_edited_preset().config).option("nozzle_diameter"))->values.size() > 1;

    auto wiping_dialog_button = wxGetApp().sidebar().get_wiping_dialog_button();
    if (wiping_dialog_button) {
        wiping_dialog_button->Show(wipe_tower_enabled && multiple_extruders);
        wiping_dialog_button->GetParent()->Layout();
    }
}


// Call a callback to update the selection of presets on the plater:
// To update the content of the selection boxes,
// to update the filament colors of the selection boxes,
// to update the "dirty" flags of the selection boxes,
// to update number of "filament" selection boxes when the number of extruders change.
void Tab::on_presets_changed()
{
    if (wxGetApp().plater() == nullptr) {
        return;
    }

    // Instead of PostEvent (EVT_TAB_PRESETS_CHANGED) just call update_presets
    wxGetApp().plater()->sidebar().update_presets(m_type);
    update_preset_description_line();

    // Printer selected at the Printer tab, update "compatible" marks at the print and filament selectors.
    for (auto t: m_dependent_tabs)
    {
        // If the printer tells us that the print or filament/sla_material preset has been switched or invalidated,
        // refresh the print or filament/sla_material tab page.
        wxGetApp().get_tab(t)->load_current_preset();
    }
    // clear m_dependent_tabs after first update from select_preset()
    // to avoid needless preset loading from update() function
    m_dependent_tabs.clear();
}

void Tab::build_preset_description_line(ConfigOptionsGroup* optgroup)
{
    auto description_line = [this](wxWindow* parent) {
        return description_line_widget(parent, &m_parent_preset_description_line);
    };

    auto detach_preset_btn = [this](wxWindow* parent) {
        add_scaled_button(parent, &m_detach_preset_btn, "lock_open_sys", _(L("Detach from system preset")), wxBU_LEFT | wxBU_EXACTFIT);
        ScalableButton* btn = m_detach_preset_btn;
        btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(btn);

        btn->Bind(wxEVT_BUTTON, [this, parent](wxCommandEvent&)
        {
        	bool system = m_presets->get_edited_preset().is_system;
        	bool dirty  = m_presets->get_edited_preset().is_dirty;
            wxString msg_text = system ? 
            	_(L("A copy of the current system preset will be created, which will be detached from the system preset.")) :
                _(L("The current custom preset will be detached from the parent system preset."));
            if (dirty) {
	            msg_text += "\n\n";
            	msg_text += _(L("Modifications to the current profile will be saved."));
            }
            msg_text += "\n\n";
            msg_text += _(L("This action is not revertable.\nDo you want to proceed?"));

            wxMessageDialog dialog(parent, msg_text, _(L("Detach preset")), wxICON_WARNING | wxYES_NO | wxCANCEL);
            if (dialog.ShowModal() == wxID_YES)
                save_preset(m_presets->get_edited_preset().is_system ? std::string() : m_presets->get_edited_preset().name, true);
        });

        btn->Hide();

        return sizer;
    };

    Line line = Line{ "", "" };
    line.full_width = 1;

    line.append_widget(description_line);
    line.append_widget(detach_preset_btn);
    optgroup->append_line(line);
}

void Tab::update_preset_description_line()
{
    const Preset* parent = m_presets->get_selected_preset_parent();
    const Preset& preset = m_presets->get_edited_preset();

    wxString description_line;

    if (preset.is_default) {
        description_line = _(L("This is a default preset."));
    } else if (preset.is_system) {
        description_line = _(L("This is a system preset."));
    } else if (parent == nullptr) {
        description_line = _(L("Current preset is inherited from the default preset."));
    } else {
        description_line = _(L("Current preset is inherited from")) + ":\n\t" + parent->name;
    }

    if (preset.is_default || preset.is_system)
        description_line += "\n\t" + _(L("It can't be deleted or modified.")) +
                            "\n\t" + _(L("Any modifications should be saved as a new preset inherited from this one.")) +
                            "\n\t" + _(L("To do that please specify a new name for the preset."));

    if (parent && parent->vendor)
    {
        description_line += "\n\n" + _(L("Additional information:")) + "\n";
        description_line += "\t" + _(L("vendor")) + ": " + (m_type == Slic3r::Preset::TYPE_PRINTER ? "\n\t\t" : "") + parent->vendor->name +
                            ", ver: " + parent->vendor->config_version.to_string();
        if (m_type == Slic3r::Preset::TYPE_PRINTER) {
            const std::string &printer_model = preset.config.opt_string("printer_model");
            if (! printer_model.empty())
                description_line += "\n\n\t" + _(L("printer model")) + ": \n\t\t" + printer_model;
            switch (preset.printer_technology()) {
            case ptFFF:
            {
                //FIXME add prefered_sla_material_profile for SLA
                const std::string              &default_print_profile = preset.config.opt_string("default_print_profile");
                const std::vector<std::string> &default_filament_profiles = preset.config.option<ConfigOptionStrings>("default_filament_profile")->values;
                if (!default_print_profile.empty())
                    description_line += "\n\n\t" + _(L("default print profile")) + ": \n\t\t" + default_print_profile;
                if (!default_filament_profiles.empty())
                {
                    description_line += "\n\n\t" + _(L("default filament profile")) + ": \n\t\t";
                    for (auto& profile : default_filament_profiles) {
                        if (&profile != &*default_filament_profiles.begin())
                            description_line += ", ";
                        description_line += profile;
                    }
                }
                break;
            }
            case ptSLA:
            {
                //FIXME add prefered_sla_material_profile for SLA
                const std::string &default_sla_material_profile = preset.config.opt_string("default_sla_material_profile");
                if (!default_sla_material_profile.empty())
                    description_line += "\n\n\t" + _(L("default SLA material profile")) + ": \n\t\t" + default_sla_material_profile;

                const std::string &default_sla_print_profile = preset.config.opt_string("default_sla_print_profile");
                if (!default_sla_print_profile.empty())
                    description_line += "\n\n\t" + _(L("default SLA print profile")) + ": \n\t\t" + default_sla_print_profile;
                break;
            }
            default: break;
            }
        }
        else if (!preset.alias.empty())
        {
            description_line += "\n\n\t" + _(L("full profile name"))     + ": \n\t\t" + preset.name;
            description_line += "\n\t"   + _(L("symbolic profile name")) + ": \n\t\t" + preset.alias;
        }
    }

    if (m_parent_preset_description_line)
        m_parent_preset_description_line->SetText(description_line, false);

    if (m_detach_preset_btn)
        m_detach_preset_btn->Show(parent && parent->is_system && !preset.is_default);
    Layout();
}

void Tab::update_frequently_changed_parameters()
{
    const bool is_fff = supports_printer_technology(ptFFF);
    auto og_freq_chng_params = wxGetApp().sidebar().og_freq_chng_params(is_fff);
    if (!og_freq_chng_params) return;

    og_freq_chng_params->set_value("support", support_combo_value_for_config(*m_config, is_fff));
    if (! is_fff)
        og_freq_chng_params->set_value("pad", pad_combo_value_for_config(*m_config));

    const std::string updated_value_key = is_fff ? "fill_density" : "pad_enable";

    const boost::any val = og_freq_chng_params->get_config_value(*m_config, updated_value_key);
    og_freq_chng_params->set_value(updated_value_key, val);

    if (is_fff)
    {
        og_freq_chng_params->set_value("brim", bool(m_config->opt_float("brim_width") > 0.0 || m_config->opt_float("brim_width_interior") > 0.0));
        update_wiping_button_visibility();
    }
}

t_change set_or_add(t_change previous, t_change toadd) {
    if (previous == nullptr)
        return toadd;
    else
        return [previous, toadd](t_config_option_key opt_key, boost::any value) {
        try {
            toadd(opt_key, value);
            previous(opt_key, value);

        }
        catch (const std::exception & ex) {
            std::cerr << "Exception while calling group event about "<<opt_key<<": " << ex.what();
            throw ex;
        }
    };
}

bool Tab::create_pages(std::string setting_type_name, int idx_page)
{
    //search for the file
    const boost::filesystem::path ui_layout_file = (boost::filesystem::path(resources_dir()) / "ui_layout" / setting_type_name).make_preferred();
    if (!boost::filesystem::exists(ui_layout_file)) {
        std::cerr << "Error: cannot create " << setting_type_name << "settings, cannot find file " << ui_layout_file << "\n";
        return false;
    }else
        std::cout << "create settings  " << setting_type_name << "\n";

    bool no_page_yet = true;
#ifdef __WXMSW__
    /* Workaround for correct layout of controls inside the created page:
     * In some _strange_ way we should we should imitate page resizing.
     */
    auto layout_page = [this](PageShp page)
    {
        const wxSize& sz = page->GetSize();
        page->SetSize(sz.x + 1, sz.y + 1);
        page->SetSize(sz);
    };
#endif
    Slic3r::GUI::PageShp current_page;
    ConfigOptionsGroupShp current_group;
    Line current_line{ "", "" };
    bool in_line = false;
    int height = 0;
    bool logs = false;

    //read file
    //std::ifstream filestream(ui_layout_file.c_str());
    boost::filesystem::ifstream filestream(ui_layout_file);
    std::string full_line;
    while (std::getline(filestream, full_line)) {
        //remove spaces
        boost::algorithm::trim(full_line);
        if (full_line.size() < 4 || full_line[0] == '#') continue;
        //get main command
        if (boost::starts_with(full_line, "logs"))
        {
            logs = true;
        }
        else if (boost::starts_with(full_line, "page"))
        {
#ifdef __WXMSW__
            if(!no_page_yet)
                layout_page(current_page);
#endif
            no_page_yet = false;
            if (in_line) {
                current_group->append_line(current_line);
                if (logs) std::cout << "add line\n";
                in_line = false;
            }
            std::vector<std::string> params;
            boost::split(params, full_line, boost::is_any_of(":"));
            for (std::string &str : params) {
                while (str.size() > 1 && (str.front() == ' ' || str.front() == '\t')) str = str.substr(1, str.size() - 1);
                while (str.size() > 1 && (str.back() == ' ' || str.back() == '\t')) str = str.substr(0, str.size() - 1);
            }
            if (params.size() < 2) std::cerr << "error, you need to add the title and icon of the page example: page:awsome page:shell, \n";
            if (params.size() < 2) continue;
            if (params.size() == 2) params.push_back("wrench");

            std::string label = L(params[params.size()-2]);

            for (int i = 1; i < params.size() - 1; i++) {
                if (params[i] == "idx")
                {
                    label = label + " " + std::to_string(int(idx_page + 1));
                }
            }

            if(logs) std::cout << "create page " << label.c_str() <<" : "<< params[params.size() - 1] << "\n";
            current_page = add_options_page(_(label), params[params.size() - 1]);
        }
        else if (boost::starts_with(full_line, "end_page"))
        {
            if (in_line) {
                current_group->append_line(current_line);
                if (logs) std::cout << "add line\n";
                in_line = false;
            }
            current_page.reset();
        }
        else if (boost::starts_with(full_line, "group"))
        {
            if (in_line) {
                current_group->append_line(current_line);
                if (logs) std::cout << "add line\n";
                in_line = false;
            }
            std::vector<std::string> params;
            boost::split(params, full_line, boost::is_any_of(":"));
            for (std::string &str : params) {
                while (str.size() > 1 && (str.front() == ' ' || str.front() == '\t')) str = str.substr(1, str.size() - 1);
                while (str.size() > 1 && (str.back() == ' ' || str.back() == '\t')) str = str.substr(0, str.size() - 1);
            }
            bool nolabel = false;
            for (int i = 1; i < params.size() - 1; i++) {
                if (params[i] == "nolabel")
                {
                    nolabel = true;
                }
            }
            
            current_group = current_page->new_optgroup(_(L(params.back())), nolabel?0:-1);
            for (int i = 1; i < params.size() - 1; i++) {
                if (boost::starts_with(params[i], "title_width$")) {
                    current_group->title_width = atoi(params[i].substr(12, params[i].size() - 12).c_str());
                }
                else if (params[i].find("label_width$") != std::string::npos)
                {
                    current_group->label_width = atoi(params[i].substr(12, params[i].size() - 12).c_str());
                }
                else if (params[i].find("sidetext_width$") != std::string::npos)
                {
                    current_group->sidetext_width = atoi(params[i].substr(15, params[i].size() - 15).c_str());
                } else if (params[i] == "extruders_count_event") {
                    TabPrinter* tab = nullptr;
                    if ((tab = dynamic_cast<TabPrinter*>(this)) == nullptr) continue;
                    current_group->m_on_change = set_or_add(current_group->m_on_change, [this, tab, current_group](t_config_option_key opt_key, boost::any value) {
                        // optgroup->get_value() return int for def.type == coInt,
                        // Thus, there should be boost::any_cast<int> !
                        // Otherwise, boost::any_cast<size_t> causes an "unhandled unknown exception"
                        if (opt_key == "extruders_count" || opt_key == "single_extruder_multi_material") {
                            size_t extruders_count = size_t(boost::any_cast<int>(current_group->get_value("extruders_count")));
                            tab->extruders_count_changed(extruders_count);
                            init_options_list(); // m_options_list should be updated before UI updating
                            update_dirty();
                            if (opt_key == "single_extruder_multi_material") { // the single_extruder_multimaterial was added to force pages
                                on_value_change(opt_key, value);                      // rebuild - let's make sure the on_value_change is not skipped

                                if (boost::any_cast<bool>(value) && tab->m_extruders_count > 1) {
                                    SuppressBackgroundProcessingUpdate sbpu;
                                    std::vector<double> nozzle_diameters = static_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values;
                                    const double frst_diam = nozzle_diameters[0];

                                    for (auto cur_diam : nozzle_diameters) {
                                        // if value is differs from first nozzle diameter value
                                        if (fabs(cur_diam - frst_diam) > EPSILON) {
                                            const wxString msg_text = _(L("Single Extruder Multi Material is selected, \n"
                                                "and all extruders must have the same diameter.\n"
                                                "Do you want to change the diameter for all extruders to first extruder nozzle diameter value?"));
                                            wxMessageDialog dialog(parent(), msg_text, _(L("Nozzle diameter")), wxICON_WARNING | wxYES_NO);

                                            DynamicPrintConfig new_conf = *m_config;
                                            if (dialog.ShowModal() == wxID_YES) {
                                                for (size_t i = 1; i < nozzle_diameters.size(); i++)
                                                    nozzle_diameters[i] = frst_diam;

                                                new_conf.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzle_diameters));
                                            } else
                                                new_conf.set_key_value("single_extruder_multi_material", new ConfigOptionBool(false));

                                            load_config(new_conf);
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    });
                } else if (params[i] == "milling_count_event") {
                    TabPrinter* tab = nullptr;
                    if ((tab = dynamic_cast<TabPrinter*>(this)) == nullptr) continue;
                    current_group->m_on_change = set_or_add(current_group->m_on_change, [this, tab, current_group](t_config_option_key opt_key, boost::any value) {
                        // optgroup->get_value() return int for def.type == coInt,
                        // Thus, there should be boost::any_cast<int> !
                        // Otherwise, boost::any_cast<size_t> causes an "unhandled unknown exception"
                        if (opt_key == "milling_count") {
                            size_t milling_count = size_t(boost::any_cast<int>(current_group->get_value("milling_count")));
                            tab->milling_count_changed(milling_count);
                            init_options_list(); // m_options_list should be updated before UI updating
                        }
                    });
                }
                else if (params[i] == "silent_mode_event") {
                    TabPrinter* tab = nullptr;
                    if ((tab = dynamic_cast<TabPrinter*>(this)) == nullptr) continue;
                    current_group->m_on_change = set_or_add(current_group->m_on_change, [this, tab](t_config_option_key opt_key, boost::any value) {
                        if (opt_key == "silent_mode") {
                            bool val = boost::any_cast<bool>(value);
                            if (tab->m_use_silent_mode != val) {
                                tab->m_rebuild_kinematics_page = true;
                                tab->m_use_silent_mode = val;
                            }
                        }
                        tab->build_unregular_pages();
                    });
                }
                else if (params[i] == "material_density_event") {
                    current_group->m_on_change = set_or_add(current_group->m_on_change, [this, current_group](t_config_option_key opt_key, boost::any value)
                    {
                        DynamicPrintConfig new_conf = *m_config;

                        if (opt_key == "bottle_volume") {
                            double new_bottle_weight = boost::any_cast<double>(value) / (new_conf.option("material_density")->getFloat() * 1000);
                            new_conf.set_key_value("bottle_weight", new ConfigOptionFloat(new_bottle_weight));
                        }
                        if (opt_key == "bottle_weight") {
                            double new_bottle_volume = boost::any_cast<double>(value)*(new_conf.option("material_density")->getFloat() * 1000);
                            new_conf.set_key_value("bottle_volume", new ConfigOptionFloat(new_bottle_volume));
                        }
                        if (opt_key == "material_density") {
                            double new_bottle_volume = new_conf.option("bottle_weight")->getFloat() * boost::any_cast<double>(value) * 1000;
                            new_conf.set_key_value("bottle_volume", new ConfigOptionFloat(new_bottle_volume));
                        }

                        load_config(new_conf);

                        if (opt_key == "bottle_volume" || opt_key == "bottle_cost") {
                            wxGetApp().sidebar().update_sliced_info_sizer();
                            wxGetApp().sidebar().Layout();
                        }
                    });
                }
            }
            if (logs) std::cout << "create group " << params.back() << "\n";
        }
        else if (boost::starts_with(full_line, "end_group"))
        {
            if (in_line) {
                current_group->append_line(current_line);
                if (logs) std::cout << "add line\n";
                in_line = false;
            }
            current_group.reset();
        }
        else if (boost::starts_with(full_line, "line"))
        {
            if (in_line) {
                current_group->append_line(current_line);
                if (logs) std::cout << "add line\n";
                in_line = false;
            }
            std::string arg = "";
            if (full_line.size() > 6 && full_line.find(":") != std::string::npos)
                arg = full_line.substr(full_line.find(":") + 1, full_line.size() - 1 - full_line.find(":"));
            while (arg.size() > 1 && (arg.back() == ' ' || arg.back() == '\t')) arg = arg.substr(0, arg.size()-1);
            current_line = { _(L(arg.c_str())), wxString{""} };
            in_line = true;
            if (logs) std::cout << "create line " << arg << "\n";
        }
        else if (boost::starts_with(full_line, "end_line"))
        {
            current_group->append_line(current_line);
            if (logs) std::cout << "add line\n";
            in_line = false;
        }
        else if (boost::starts_with(full_line, "setting"))
        {
            std::vector<std::string> params;
            boost::split(params, full_line, boost::is_any_of(":"));
            for (std::string &str : params) {
                while (str.size() > 1 && (str.front() == ' ' || str.front() == '\t')) str = str.substr(1, str.size() - 1);
                while (str.size() > 1 && (str.back() == ' ' || str.back() == '\t')) str = str.substr(0, str.size() - 1);
            }

            std::string setting_id = "";
            if (params.size() > 1) setting_id = params.back();
            if (setting_id.size() < 2) continue;
            if (!m_config->has(setting_id)) {
                std::cerr << "No " << setting_id << " in ConfigOptionsGroup config, tab "<< setting_type_name <<".\n";
                continue;
            }

            if (setting_id == "compatible_printers") {
                create_line_with_widget(current_group.get(),"compatible_printers", [this](wxWindow* parent) {
                    return compatible_widget_create(parent, m_compatible_printers);
                });
                continue;
            }
            else if (setting_id == "compatible_prints") {
                create_line_with_widget(current_group.get(), "compatible_prints", [this](wxWindow* parent) {
                    return compatible_widget_create(parent, m_compatible_prints);
                });
                continue;
            }

            int id = -1;
            for (int i = 1; i < params.size() - 1; i++) {
                if (boost::starts_with(params[i], "id$"))
                    id = atoi(params[i].substr(3, params[i].size() - 3).c_str());
                else if (params[i] == "idx")
                    id = idx_page;
            }

            Option option = current_group->get_option(setting_id, id);

            bool colored = false;
            for (int i = 1; i < params.size() - 1; i++) {
                if (params[i] == "simple")
                {
                    option.opt.mode = ConfigOptionMode::comSimple;
                }
                else if (params[i] == "advanced")
                {
                    option.opt.mode = ConfigOptionMode::comAdvanced;
                }
                else if (params[i] == "expert")
                {
                    option.opt.mode = ConfigOptionMode::comExpert;
                }
                else if (params[i] == "full_label")
                {
                    option.opt.label = option.opt.full_label;
                }
                else if (params[i].find("label$") != std::string::npos)
                {
                    option.opt.label = params[i].substr(6, params[i].size() - 6);
                }
                else if (boost::starts_with(params[i], "label_width$")) {
                    option.opt.label_width = atoi(params[i].substr(12, params[i].size() - 12).c_str());
                }
                else if (params[i].find("sidetext$") != std::string::npos)
                {
                    option.opt.sidetext = params[i].substr(9, params[i].size() - 9);
                }
                else if (params[i].find("sidetext_width$") != std::string::npos)
                {
                    option.opt.sidetext_width = atoi(params[i].substr(15, params[i].size() - 15).c_str());
                }
                else if (params[i] == "full_width") {
                    option.opt.full_width = true;
                }
                else if (boost::starts_with(params[i], "width$")) {
                    option.opt.width = atoi(params[i].substr(6, params[i].size() - 6).c_str());
                }
                else if (boost::starts_with(params[i], "height$")) {
                    option.opt.height = atoi(params[i].substr(7, params[i].size() - 7).c_str());
                }
                else if (params[i] == "color") {
                    colored = true;
                }
            }

            if(height>0)
                option.opt.height = height;

            if (!in_line) {
                if (colored) {
                    m_colored_Labels[setting_id] = nullptr;
                    current_group->append_line(current_group->create_single_option_line(option), &m_colored_Labels[setting_id]);
                } else
                    current_group->append_line(current_group->create_single_option_line(option));
            } else
                current_line.append_option(option);
            if (logs) std::cout << "create setting " << setting_id <<"  with label "<< option.opt.label << "and height "<< option.opt.height<<" fw:"<< option.opt.full_width << "\n";
        }
        else if (boost::starts_with(full_line, "height")) {
            std::string arg = "";
            if (full_line.size() > 6 && full_line.find(":") != std::string::npos)
                arg = full_line.substr(full_line.find(":") + 1, full_line.size() - 1 - full_line.find(":"));
            while (arg.size() > 1 && (arg.back() == ' ' || arg.back() == '\t')) arg = arg.substr(0, arg.size() - 1);
            height = atoi(arg.c_str());
        }
        else if (boost::starts_with(full_line, "recommended_thin_wall_thickness_description")) {
            TabPrint* tab = nullptr;
            if ((tab = dynamic_cast<TabPrint*>(this)) == nullptr) continue;
            current_line = { "", "" };
            current_line.full_width = 1;
            current_line.widget = [this, tab](wxWindow* parent) {
                return description_line_widget(parent, &(tab->m_recommended_thin_wall_thickness_description_line));
            };
            current_group->append_line(current_line);
        }
        else if (boost::starts_with(full_line, "top_bottom_shell_thickness_explanation")) {
            TabPrint* tab = nullptr;
            if ((tab = dynamic_cast<TabPrint*>(this)) == nullptr) continue;
            current_line = { "", "" };
            current_line.full_width = 1;
            current_line.widget = [this, tab](wxWindow* parent) {
                return description_line_widget(parent, &(tab->m_top_bottom_shell_thickness_explanation));
            };
            current_group->append_line(current_line);
        }
        else if (boost::starts_with(full_line, "parent_preset_description")) {
            build_preset_description_line(current_group.get());
        }
        else if (boost::starts_with(full_line, "cooling_description")) {
            TabFilament* tab = nullptr;
            if ((tab = dynamic_cast<TabFilament*>(this)) == nullptr) continue;
            current_line = Line{ "", "" };
            current_line.full_width = 1;
            current_line.widget = [this, tab](wxWindow* parent) {
                return description_line_widget(parent, &(tab->m_cooling_description_line));
            };
            current_group->append_line(current_line);
        }
        else if (boost::starts_with(full_line, "volumetric_speed_description")) {
            TabFilament* tab = nullptr;
            if ((tab = dynamic_cast<TabFilament*>(this)) == nullptr) continue;
            current_line = Line{ "", "" };
            current_line.full_width = 1;
            current_line.widget = [this, tab](wxWindow* parent) {
                return description_line_widget(parent, &(tab->m_volumetric_speed_description_line));
            };
            current_group->append_line(current_line);
        }
        else if (boost::starts_with(full_line, "filament_ramming_parameters")) {
            Line thisline = current_group->create_single_option_line("filament_ramming_parameters");// { _(L("Ramming")), "" };
            thisline.widget = [this](wxWindow* parent) {
                auto ramming_dialog_btn = new wxButton(parent, wxID_ANY, _(L("Ramming settings")) + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
                ramming_dialog_btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
                auto sizer = new wxBoxSizer(wxHORIZONTAL);
                sizer->Add(ramming_dialog_btn);

                ramming_dialog_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent& e)
                {
                    RammingDialog dlg(this, (m_config->option<ConfigOptionStrings>("filament_ramming_parameters"))->get_at(0));
                    if (dlg.ShowModal() == wxID_OK)
                        (m_config->option<ConfigOptionStrings>("filament_ramming_parameters"))->get_at(0) = dlg.get_parameters();
                }));
                return sizer;
            };
            current_group->append_line(thisline);
        }
        else if (boost::starts_with(full_line, "filament_overrides_page")) {
            TabFilament* tab = nullptr;
            if ((tab = dynamic_cast<TabFilament*>(this)) == nullptr) continue;
            tab->add_filament_overrides_page();
        }
        else if (full_line == "unregular_pages") {
            TabPrinter* tab = nullptr;
            if ((tab = dynamic_cast<TabPrinter*>(this)) == nullptr) continue;
            tab->build_unregular_pages();
        }
        else if (full_line == "printhost") {
            TabPrinter* tab = nullptr;
            if ((tab = dynamic_cast<TabPrinter*>(this)) == nullptr) continue;
            tab->build_printhost(current_group.get());
        }
        else if (full_line == "bed_shape"){
            TabPrinter* tab = nullptr;
            if ((tab = dynamic_cast<TabPrinter*>(this)) == nullptr) continue;
            create_line_with_widget(current_group.get(), "bed_shape", [tab](wxWindow* parent) {
                return 	tab->create_bed_shape_widget(parent);
            });
        } else if (full_line == "extruders_count") {
            ConfigOptionDef def;
            def.type = coInt,
                def.set_default_value(new ConfigOptionInt(1));
            def.label = L("Extruders");
            def.tooltip = L("Number of extruders of the printer.");
            def.min = 1;
            def.mode = comAdvanced;
            Option option(def, "extruders_count");
            current_group->append_single_option_line(option);
        } else if (full_line == "milling_count") {
            ConfigOptionDef def;
            def.type = coInt,
                def.set_default_value(new ConfigOptionInt(0));
            def.label = L("Milling cutters");
            def.tooltip = L("Number of milling heads.");
            def.min = 0;
            def.mode = comAdvanced;
            Option option(def, "milling_count");
            current_group->append_single_option_line(option);
        } else if (full_line == "update_nozzle_diameter") {
            current_group->m_on_change = set_or_add(current_group->m_on_change, [this, idx_page](const t_config_option_key& opt_key, boost::any value)
            {
                TabPrinter* tab = nullptr;
                if ((tab = dynamic_cast<TabPrinter*>(this)) == nullptr) return;
                if (m_config->opt_bool("single_extruder_multi_material") && tab->m_extruders_count > 1 && opt_key.find_first_of("nozzle_diameter") != std::string::npos)
                {
                    SuppressBackgroundProcessingUpdate sbpu;
                    const double new_nd = boost::any_cast<double>(value);
                    std::vector<double> nozzle_diameters = static_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values;

                    // if value was changed
                    if (fabs(nozzle_diameters[idx_page == 0 ? 1 : 0] - new_nd) > EPSILON)
                    {
                        const wxString msg_text = _(L("This is a single extruder multimaterial printer, diameters of all extruders "
                            "will be set to the new value. Do you want to proceed?"));
                        wxMessageDialog dialog(parent(), msg_text, _(L("Nozzle diameter")), wxICON_WARNING | wxYES_NO);

                        DynamicPrintConfig new_conf = *m_config;
                        if (dialog.ShowModal() == wxID_YES) {
                            for (size_t i = 0; i < nozzle_diameters.size(); i++) {
                                if (i == idx_page)
                                    continue;
                                nozzle_diameters[i] = new_nd;
                            }
                        } else
                            nozzle_diameters[idx_page] = nozzle_diameters[idx_page == 0 ? 1 : 0];

                        new_conf.set_key_value("nozzle_diameter", new ConfigOptionFloats(nozzle_diameters));
                        load_config(new_conf);
                    }
                }

                update();
            });
        }else if(full_line == "reset_to_filament_color") {
            TabPrinter* tab = nullptr;
            if ((tab = dynamic_cast<TabPrinter*>(this)) == nullptr) continue;
            widget_t reset_to_filament_color = [this, idx_page, tab](wxWindow* parent) -> wxBoxSizer* {
                add_scaled_button(parent, &tab->m_reset_to_filament_color, "undo",
                    _(L("Reset to Filament Color")), wxBU_LEFT | wxBU_EXACTFIT);
                ScalableButton* btn = tab->m_reset_to_filament_color;
                btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
                wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
                sizer->Add(btn);

                btn->Bind(wxEVT_BUTTON, [this, idx_page](wxCommandEvent& e)
                {
                    std::vector<std::string> colors = static_cast<const ConfigOptionStrings*>(m_config->option("extruder_colour"))->values;
                    colors[idx_page] = "";

                    DynamicPrintConfig new_conf = *m_config;
                    new_conf.set_key_value("extruder_colour", new ConfigOptionStrings(colors));
                    load_config(new_conf);

                    update_dirty();
                    update();
                });

                return sizer;
            };
            current_line = current_group->create_single_option_line("extruder_colour", idx_page);
            current_line.append_widget(reset_to_filament_color);
            current_group->append_line(current_line);
        }
    }
   /* fs::ifstream inFileUTF(ui_layout_file);tyj
    std::wbuffer_convert<std::codecvt_utf8<wchar_t>> inFilebufConverted(inFileUTF.rdbuf());
    std::wistream inFileConverted(&inFilebufConverted);
    for (std::wstring s; getline(inFileConverted, s); )
    {
        std::wcout << p_list_utf.c_str() << '\n' << s << '\n';
        if (fs::exists(s))
            std::wcout << "File exists!\n";
        else
            std::wcout << "File DOES NOT exist!\n";
    }*/
#ifdef __WXMSW__
    if (!no_page_yet)
        layout_page(current_page);
#endif

    if(logs) std::cout << "END create settings  " << setting_type_name << "\n";

    return !no_page_yet;
}

void TabPrint::build()
{
    m_presets = &m_preset_bundle->prints;
    load_initial_data();
    if (create_pages("print.ui")) return;

}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabPrint::reload_config()
{
    this->compatible_widget_reload(m_compatible_printers);
    Tab::reload_config();
}

void TabPrint::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME

    m_update_cnt++;
//	Freeze();

    m_config_manipulation.update_print_fff_config(m_config, true);

    if (m_recommended_thin_wall_thickness_description_line)
        m_recommended_thin_wall_thickness_description_line->SetText(
            from_u8(PresetHints::recommended_thin_wall_thickness(*m_preset_bundle)));
    if(m_top_bottom_shell_thickness_explanation)
        m_top_bottom_shell_thickness_explanation->SetText(
            from_u8(PresetHints::top_bottom_shell_thickness_explanation(*m_preset_bundle)));
    Layout();

//	Thaw();
    m_update_cnt--;

    if (m_update_cnt==0) {
        m_config_manipulation.toggle_print_fff_options(m_config);

        // update() could be called during undo/redo execution
        // Update of objectList can cause a crash in this case (because m_objects doesn't match ObjectList) 
        if (!wxGetApp().plater()->inside_snapshot_capture())
            wxGetApp().obj_list()->update_and_show_object_settings_item();

        wxGetApp().mainframe->on_config_changed(m_config);
    }
}

void TabPrint::OnActivate()
{
    if (m_recommended_thin_wall_thickness_description_line)
        m_recommended_thin_wall_thickness_description_line->SetText(
            from_u8(PresetHints::recommended_thin_wall_thickness(*m_preset_bundle)));
    if(m_top_bottom_shell_thickness_explanation)
        m_top_bottom_shell_thickness_explanation->SetText(
        from_u8(PresetHints::top_bottom_shell_thickness_explanation(*m_preset_bundle)));
    Tab::OnActivate();
}

void TabFilament::add_filament_overrides_page()
{
    PageShp page = add_options_page(_(L("Filament Overrides")), "wrench");
    ConfigOptionsGroupShp optgroup = page->new_optgroup(_(L("Retraction")));

    auto append_single_option_line = [optgroup, this](const std::string& opt_key, int opt_index)
    {
        Line line {"",""};
        if (opt_key == "filament_retract_lift_above" || opt_key == "filament_retract_lift_below") {
            Option opt = optgroup->get_option(opt_key);
            opt.opt.label = opt.opt.get_full_label();
            line = optgroup->create_single_option_line(opt);
        }
        else
            line = optgroup->create_single_option_line(optgroup->get_option(opt_key));

        line.near_label_widget = [this, optgroup, opt_key, opt_index](wxWindow* parent) {
            wxCheckBox* check_box = new wxCheckBox(parent, wxID_ANY, "");

            check_box->Bind(wxEVT_CHECKBOX, [this, optgroup, opt_key, opt_index](wxCommandEvent& evt) {
                const bool is_checked = evt.IsChecked();
                Field* field = optgroup->get_fieldc(opt_key, opt_index);
                if (field) {
                    field->toggle(is_checked);
                    if (is_checked)
                        field->set_last_meaningful_value();
                    else
                        field->set_na_value();
                }
            }, check_box->GetId());

            m_overrides_options[opt_key] = check_box;
            return check_box;
        };

        optgroup->append_line(line);
    };

    const int extruder_idx = 0; // #ys_FIXME

    for (const std::string opt_key : {  "filament_retract_length",
                                        "filament_retract_lift",
                                        "filament_retract_lift_above",
                                        "filament_retract_lift_below",
                                        "filament_retract_speed",
                                        "filament_deretract_speed",
                                        "filament_retract_restart_extra",
                                        "filament_retract_before_travel",
                                        "filament_retract_layer_change",
                                        "filament_wipe",
                                        "filament_wipe_extra_perimeter",
                                        "filament_retract_before_wipe"
                                     })
        append_single_option_line(opt_key, extruder_idx);
}

void TabFilament::update_filament_overrides_page()
{
    const auto page_it = std::find_if(m_pages.begin(), m_pages.end(), [](const PageShp page) {return page->title() == _(L("Filament Overrides")); });
    if (page_it == m_pages.end())
        return;
    PageShp page = *page_it;

    const auto og_it = std::find_if(page->m_optgroups.begin(), page->m_optgroups.end(), [](const ConfigOptionsGroupShp og) {return og->title == _(L("Retraction")); });
    if (og_it == page->m_optgroups.end())
        return;
    ConfigOptionsGroupShp optgroup = *og_it;

    std::vector<std::string> opt_keys = {   "filament_retract_length",
                                            "filament_retract_lift",
                                            "filament_retract_lift_above",
                                            "filament_retract_lift_below",
                                            "filament_retract_speed",
                                            "filament_deretract_speed",
                                            "filament_retract_restart_extra",
                                            "filament_retract_before_travel",
                                            "filament_retract_layer_change",
                                            "filament_wipe",
                                            "filament_wipe_extra_perimeter",
                                            "filament_retract_before_wipe"
                                        };

    const int extruder_idx = 0; // #ys_FIXME

    const bool have_retract_length = m_config->option("filament_retract_length")->is_nil() ||
                                     m_config->opt_float("filament_retract_length", extruder_idx) > 0;

    for (const std::string& opt_key : opt_keys)
    {
        bool is_checked = opt_key=="filament_retract_length" ? true : have_retract_length;
        m_overrides_options[opt_key]->Enable(is_checked);

        is_checked &= !m_config->option(opt_key)->is_nil();
        m_overrides_options[opt_key]->SetValue(is_checked);

        Field* field = optgroup->get_fieldc(opt_key, extruder_idx);
        if (field)
            field->toggle(is_checked);
    }
}

void TabFilament::build()
{
    m_presets = &m_preset_bundle->filaments;
    load_initial_data();
    if (create_pages("filament.ui")) return;

}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabFilament::reload_config()
{
    this->compatible_widget_reload(m_compatible_printers);
    this->compatible_widget_reload(m_compatible_prints);
    Tab::reload_config();
}

void TabFilament::update_volumetric_flow_preset_hints()
{
    wxString text;
    try {
        text = from_u8(PresetHints::maximum_volumetric_flow_description(*m_preset_bundle));
    } catch (std::exception &ex) {
        text = _(L("Volumetric flow hints not available")) + "\n\n" + from_u8(ex.what());
    }
    if(m_volumetric_speed_description_line)
        m_volumetric_speed_description_line->SetText(text);
}

void TabFilament::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        return; // ys_FIXME

    m_update_cnt++;

    wxString text = from_u8(PresetHints::cooling_description(m_presets->get_edited_preset()));
    if(m_cooling_description_line)
        m_cooling_description_line->SetText(text);
    this->update_volumetric_flow_preset_hints();
    Layout();

    bool cooling = m_config->opt_bool("cooling", 0);
    bool fan_always_on = cooling || m_config->opt_bool("fan_always_on", 0);

    //get_field("max_fan_speed")->toggle(m_config->opt_int("fan_below_layer_time", 0) > 0);
    Field* min_print_speed_field = get_field("min_print_speed");
    if(min_print_speed_field)
        min_print_speed_field->toggle(m_config->opt_int("slowdown_below_layer_time", 0) > 0);

    // hidden 'cooling', it's now deactivated.
    //for (auto el : { "max_fan_speed", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed" })
    //    get_field(el)->toggle(cooling);


    //for (auto el : { "min_fan_speed", "disable_fan_first_layers" })
    //    get_field(el)->toggle(fan_always_on);

    Field* max_fan_speed_field = get_field("max_fan_speed");
    if (max_fan_speed_field)
        max_fan_speed_field->toggle(m_config->opt_int("fan_below_layer_time", 0) > 0 || m_config->opt_int("slowdown_below_layer_time", 0) > 0);

    update_filament_overrides_page();

    m_update_cnt--;

    if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabFilament::OnActivate()
{
    this->update_volumetric_flow_preset_hints();
    Tab::OnActivate();
}

wxSizer* Tab::description_line_widget(wxWindow* parent, ogStaticText* *StaticText)
{
    *StaticText = new ogStaticText(parent, "");

//	auto font = (new wxSystemSettings)->GetFont(wxSYS_DEFAULT_GUI_FONT);
    (*StaticText)->SetFont(wxGetApp().normal_font());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(*StaticText, 1, wxEXPAND|wxALL, 0);
    return sizer;
}

bool Tab::current_preset_is_dirty()
{
    return m_presets->current_is_dirty();
}

void TabPrinter::build_printhost(ConfigOptionsGroup *optgroup)
{
    const PrinterTechnology tech = m_presets->get_selected_preset().printer_technology();

    // Only offer the host type selection for FFF, for SLA it's always the SL1 printer (at the moment)
    if (tech == ptFFF) {
        optgroup->append_single_option_line("host_type");
    }

    auto printhost_browse = [=](wxWindow* parent) {
        add_scaled_button(parent, &m_printhost_browse_btn, "browse", _(L("Browse")) + " "+ dots, wxBU_LEFT | wxBU_EXACTFIT);
        ScalableButton* btn = m_printhost_browse_btn;
        btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(btn);

        btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent &e) {
            BonjourDialog dialog(parent, tech);
            if (dialog.show_and_lookup()) {
                optgroup->set_value("print_host", std::move(dialog.get_selected()), true);
                optgroup->get_field("print_host")->field_changed();
            }
        });

        return sizer;
    };

    auto print_host_test = [this](wxWindow* parent) {
        add_scaled_button(parent, &m_print_host_test_btn, "test", _(L("Test")), wxBU_LEFT | wxBU_EXACTFIT);
        ScalableButton* btn = m_print_host_test_btn;
        btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(btn);

        btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
            std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
            if (! host) {
                const wxString text = _(L("Could not get a valid Printer Host reference"));
                show_error(this, text);
                return;
            }
            wxString msg;
            if (host->test(msg)) {
                show_info(this, host->get_test_ok_msg(), _(L("Success!")));
            } else {
                show_error(this, host->get_test_failed_msg(msg));
            }
        });

        return sizer;
    };

    // Set a wider width for a better alignment
    Option option = optgroup->get_option("print_host");
    option.opt.width = Field::def_width_wider();
    Line host_line = optgroup->create_single_option_line(option);
    host_line.append_widget(printhost_browse);
    host_line.append_widget(print_host_test);
    optgroup->append_line(host_line);
    option = optgroup->get_option("printhost_apikey");
    option.opt.width = Field::def_width_wider();
    optgroup->append_single_option_line(option);

    const auto ca_file_hint = _utf8(L("HTTPS CA file is optional. It is only needed if you use HTTPS with a self-signed certificate."));

    if (Http::ca_file_supported()) {
        option = optgroup->get_option("printhost_cafile");
        option.opt.width = Field::def_width_wider();
        Line cafile_line = optgroup->create_single_option_line(option);

        auto printhost_cafile_browse = [this, optgroup] (wxWindow* parent) {
            auto btn = new wxButton(parent, wxID_ANY, " " + _(L("Browse"))+" " +dots, wxDefaultPosition, wxDefaultSize, wxBU_LEFT);
            btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());
            btn->SetBitmap(create_scaled_bitmap("browse"));
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(btn);

            btn->Bind(wxEVT_BUTTON, [this, optgroup] (wxCommandEvent e) {
                static const auto filemasks = _(L("Certificate files (*.crt, *.pem)|*.crt;*.pem|All files|*.*"));
                wxFileDialog openFileDialog(this, _(L("Open CA certificate file")), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
                if (openFileDialog.ShowModal() != wxID_CANCEL) {
                    optgroup->set_value("printhost_cafile", std::move(openFileDialog.GetPath()), true);
                    optgroup->get_field("printhost_cafile")->field_changed();
                }
            });

            return sizer;
        };

        cafile_line.append_widget(printhost_cafile_browse);
        optgroup->append_line(cafile_line);

        Line cafile_hint { "", "" };
        cafile_hint.full_width = 1;
        cafile_hint.widget = [this, ca_file_hint](wxWindow* parent) {
            auto txt = new wxStaticText(parent, wxID_ANY, ca_file_hint);
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(txt);
            return sizer;
        };
        optgroup->append_line(cafile_hint);
    } else {
        Line line { "", "" };
        line.full_width = 1;

        line.widget = [ca_file_hint] (wxWindow* parent) {
            std::string info = _utf8(L("HTTPS CA File")) + ":\n\t" +
                (boost::format(_utf8(L("On this system, %s uses HTTPS certificates from the system Certificate Store or Keychain."))) % SLIC3R_APP_NAME).str() +
                "\n\t" + _utf8(L("To use a custom CA file, please import your CA file into Certificate Store / Keychain."));

            auto txt = new wxStaticText(parent, wxID_ANY, from_u8((boost::format("%1%\n\n\t%2%") % info % ca_file_hint).str()));
/*    % (boost::format(_utf8(L("HTTPS CA File:\n\
    \tOn this system, %s uses HTTPS certificates from the system Certificate Store or Keychain.\n\
    \tTo use a custom CA file, please import your CA file into Certificate Store / Keychain."))) % SLIC3R_APP_NAME).str()
    % std::string(ca_file_hint.ToUTF8())).str()));
*/            txt->SetFont(Slic3r::GUI::wxGetApp().normal_font());
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(txt, 1, wxEXPAND);
            return sizer;
        };

        optgroup->append_line(line);
    }
}

void TabPrinter::build()
{
    m_presets = &m_preset_bundle->printers;
    load_initial_data();

    m_printer_technology = m_presets->get_selected_preset().printer_technology();

    m_presets->get_selected_preset().printer_technology() == ptSLA ? build_sla() : build_fff();
}

void TabPrinter::build_fff()
{
    if (!m_pages.empty())
        m_pages.resize(0);
    // to avoid redundant memory allocation / deallocation during extruders count changing
    m_pages.reserve(30);

    auto* nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
    m_initial_extruders_count = m_extruders_count = nozzle_diameter->values.size();
    wxGetApp().sidebar().update_objects_list_extruder_column(m_initial_extruders_count);

    auto* milling_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("milling_diameter"));
    m_initial_milling_count = m_milling_count = milling_diameter->values.size();

    const Preset* parent_preset = m_presets->get_selected_preset_parent();
    m_sys_extruders_count = parent_preset == nullptr ? 0 :
        static_cast<const ConfigOptionFloats*>(parent_preset->config.option("nozzle_diameter"))->values.size();
    m_sys_milling_count = parent_preset == nullptr ? 0 :
        static_cast<const ConfigOptionFloats*>(parent_preset->config.option("milling_diameter"))->values.size();

    if (create_pages("printer_fff.ui")) return;

}

void TabPrinter::build_sla()
{
    if (!m_pages.empty())
        m_pages.resize(0);

    if (create_pages("printer_sla.ui")) return;

}

void TabPrinter::update_serial_ports()
{
    Field *field = get_field("serial_port");
    Choice *choice = static_cast<Choice *>(field);
    choice->set_values(Utils::scan_serial_ports());
}

void TabPrinter::extruders_count_changed(size_t extruders_count)
{
    bool is_count_changed = false;
    if (m_extruders_count != extruders_count) {
        m_extruders_count = extruders_count;
        m_preset_bundle->printers.get_edited_preset().set_num_extruders(extruders_count);
        m_preset_bundle->update_multi_material_filament_presets();
        is_count_changed = true;
    } else if (m_extruders_count == 1 &&
        m_preset_bundle->project_config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values.size() > 1)
        m_preset_bundle->update_multi_material_filament_presets();

    /* This function should be call in any case because of correct updating/rebuilding
     * of unregular pages of a Printer Settings
     */
    build_unregular_pages();

    if (is_count_changed) {
        on_value_change("extruders_count", (int)extruders_count);
        wxGetApp().sidebar().update_objects_list_extruder_column(extruders_count);
    }
}

void TabPrinter::milling_count_changed(size_t milling_count)
{
    bool is_count_changed = false;
    if (m_milling_count != milling_count) {
        m_milling_count = milling_count;
        m_preset_bundle->printers.get_edited_preset().set_num_milling(milling_count);
        is_count_changed = true;
    }

    /* This function should be call in any case because of correct updating/rebuilding
     * of unregular pages of a Printer Settings
     */
    build_unregular_pages();

    //no gui listing for now
    //if (is_count_changed) {
    //    on_value_change("milling_count", milling_count);
    //    wxGetApp().sidebar().update_objects_list_milling_column(milling_count);
    //}
}

void TabPrinter::append_option_line_kinematics(ConfigOptionsGroupShp optgroup, const std::string opt_key)
{
    Option option = optgroup->get_option(opt_key, 0);
    Line line = Line{ _(option.opt.full_label), "" };
    option.opt.width = 10;
    line.append_option(option);
    if (m_use_silent_mode) {
        option = optgroup->get_option(opt_key, 1);
        option.opt.width = 10;
        line.append_option(option);
    }
    optgroup->append_line(line);
}

PageShp TabPrinter::build_kinematics_page()
{
    auto page = add_options_page(_(L("Machine limits")), "cog", true);
    ConfigOptionsGroupShp optgroup;
    if (m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value != gcfMarlin && m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value != gcfLerdge) {
        optgroup = page->new_optgroup(_(L("not-marlin/lerdge firmware compensation"))); 
        optgroup->append_single_option_line("time_estimation_compensation");
    }
    optgroup = page->new_optgroup(_(L("Machine Limits")));
    Line current_line = Line{ "", "" };
    current_line.full_width = 1;
    current_line.widget = [this](wxWindow* parent) {
        ogStaticText* text;
        auto result = description_line_widget(parent, &text);
        text->SetText(_(L("Description: The information below is used to calculate estimated printing time and as safegard when generating gcode"
            " (even if the acceleration is set to 3000 in the print profile, if this is at 1500, it won't export a gcode that will tell to go over 1500)."
            " You can also export these limits to the start gcode via the checkbox above (the output depends on the selected firmare).")));
        return result;
    };
    optgroup->append_line(current_line);
    optgroup->append_single_option_line("print_machine_envelope");

    if (m_use_silent_mode) {
        // Legend for OptionsGroups
        optgroup = page->new_optgroup("");
        optgroup->set_show_modified_btns_val(false);
        optgroup->title_width = 23;// 230;
        auto line = Line{ "", "" };

        ConfigOptionDef def;
        def.type = coString;
        def.width = 18;
        def.gui_type = "legend";
        def.mode = comAdvanced;
        def.tooltip = L("Values in this column are for Normal mode");
        def.set_default_value(new ConfigOptionString{ _(L("Normal")).ToUTF8().data() });

        auto option = Option(def, "full_power_legend");
        line.append_option(option);

        def.tooltip = L("Values in this column are for Stealth mode");
        def.set_default_value(new ConfigOptionString{ _(L("Stealth")).ToUTF8().data() });
        option = Option(def, "silent_legend");
        line.append_option(option);

        optgroup->append_line(line);
    }

    std::vector<std::string> axes{ "x", "y", "z", "e" };
    optgroup = page->new_optgroup(_(L("Maximum feedrates")));
        for (const std::string &axis : axes) {
            append_option_line_kinematics(optgroup, "machine_max_feedrate_" + axis);
        }

    optgroup = page->new_optgroup(_(L("Maximum accelerations")));
        for (const std::string &axis : axes) {
            append_option_line_kinematics(optgroup, "machine_max_acceleration_" + axis);
        }
        append_option_line_kinematics(optgroup, "machine_max_acceleration_extruding");
        append_option_line_kinematics(optgroup, "machine_max_acceleration_retracting");
        append_option_line_kinematics(optgroup, "machine_max_acceleration_travel");

    optgroup = page->new_optgroup(_(L("Jerk limits")));
        for (const std::string &axis : axes) {
            append_option_line_kinematics(optgroup, "machine_max_jerk_" + axis);
        }

    optgroup = page->new_optgroup(_(L("Minimum feedrates")));
        append_option_line_kinematics(optgroup, "machine_min_extruding_rate");
        append_option_line_kinematics(optgroup, "machine_min_travel_rate");

    return page;
}

/* Previous name build_extruder_pages().
 *
 * This function was renamed because of now it implements not just an extruder pages building,
 * but "Machine limits" and "Single extruder MM setup" too
 * (These pages can changes according to the another values of a current preset)
 * */
void TabPrinter::build_unregular_pages()
{
    size_t		n_before_extruders = std::min(size_t(2), m_pages.size());			//	Count of pages before Extruder pages
    bool changed = false;

    /* ! Freeze/Thaw in this function is needed to avoid call OnPaint() for erased pages
     * and be cause of application crash, when try to change Preset in moment,
     * when one of unregular pages is selected.
     *  */
    Freeze();

#ifdef __WXMSW__
    /* Workaround for correct layout of controls inside the created page:
     * In some _strange_ way we should we should imitate page resizing.
     */
    auto layout_page = [this](PageShp page)
    {
        const wxSize& sz = page->GetSize();
        page->SetSize(sz.x + 1, sz.y + 1);
        page->SetSize(sz);
    };
#endif //__WXMSW__

    // Add/delete Kinematics page
    size_t existed_page = 0;
    for (size_t i = 0; i < m_pages.size(); ++i) { // first make sure it's not there already
        if (m_pages[i]->title().find(_(L("Machine limits"))) != std::string::npos) {
            if (m_rebuild_kinematics_page)
                m_pages.erase(m_pages.begin() + i);
            else
                existed_page = i;
            n_before_extruders = i;
            break;
        }
    }
    if (existed_page < n_before_extruders) {
        auto page = build_kinematics_page();
        changed = true;
        m_rebuild_kinematics_page = false;
#ifdef __WXMSW__
        layout_page(page);
#endif
        m_pages.insert(m_pages.begin() + n_before_extruders, page);
    }

    n_before_extruders++; // kinematic page
    size_t		n_after_single_extruder_MM = 2; //	Count of pages after single_extruder_multi_material page

    if (m_extruders_count_old == m_extruders_count ||
        (m_has_single_extruder_MM_page && m_extruders_count == 1))
    {
        // if we have a single extruder MM setup, add a page with configuration options:
        for (size_t i = 0; i < m_pages.size(); ++i) // first make sure it's not there already
            if (m_pages[i]->title().find(_(L("Single extruder MM setup"))) != std::string::npos) {
                m_pages.erase(m_pages.begin() + i);
                changed = true;
                break;
            }
        m_has_single_extruder_MM_page = false;
    }
    if (m_extruders_count > 1 && m_config->opt_bool("single_extruder_multi_material") && !m_has_single_extruder_MM_page) {
        // create a page, but pretend it's an extruder page, so we can add it to m_pages ourselves
        auto page = add_options_page(_(L("Single extruder MM setup")), "printer", true);
        auto optgroup = page->new_optgroup(_(L("Single extruder multimaterial parameters")));
        optgroup->append_single_option_line("cooling_tube_retraction");
        optgroup->append_single_option_line("cooling_tube_length");
        optgroup->append_single_option_line("parking_pos_retraction");
        optgroup->append_single_option_line("extra_loading_move");
        optgroup->append_single_option_line("high_current_on_filament_swap");
        optgroup = page->new_optgroup(_(L("Advanced wipe tower purge volume calculs")));
        optgroup->append_single_option_line("wipe_advanced");
        optgroup->append_single_option_line("wipe_advanced_nozzle_melted_volume");
        optgroup->append_single_option_line("wipe_advanced_multiplier");
        optgroup->append_single_option_line("wipe_advanced_algo");
        m_pages.insert(m_pages.end() - n_after_single_extruder_MM, page);
        m_has_single_extruder_MM_page = true;
        changed = true;
    }

    // Build missed extruder pages
    for (size_t extruder_idx = m_extruders_count_old; extruder_idx < m_extruders_count; ++extruder_idx) {

        if (this->create_pages("extruder.ui", extruder_idx)) {
            if (m_pages.size() > n_before_extruders + 1)
                std::rotate(m_pages.begin() + n_before_extruders + extruder_idx, m_pages.end() - 1, m_pages.end());
            changed = true;
        }

    }
    // # remove extra pages
    if (m_extruders_count < m_extruders_count_old) {
        m_pages.erase(m_pages.begin() + n_before_extruders + m_extruders_count,
            m_pages.begin() + n_before_extruders + m_extruders_count_old);
        changed = true;
    }
    m_extruders_count_old = m_extruders_count;

    // Build missed milling pages
    for (size_t milling_idx = m_milling_count_old; milling_idx < m_milling_count; ++milling_idx) {

        if (this->create_pages("milling.ui", milling_idx)) {
            std::rotate(m_pages.begin() + n_before_extruders + m_extruders_count + milling_idx, m_pages.end() - 1, m_pages.end());
            changed = true;
        }

    }
    // # remove extra pages
    if (m_milling_count < m_milling_count_old) {
        m_pages.erase(m_pages.begin() + n_before_extruders + m_extruders_count + m_milling_count,
            m_pages.begin() + n_before_extruders + m_extruders_count + m_milling_count_old);
        changed = true;
    }
    m_milling_count_old = m_milling_count;

    Thaw();

    if(changed)
        rebuild_page_tree();

    // Reload preset pages with current configuration values
    reload_config();
}

// this gets executed after preset is loaded and before GUI fields are updated
void TabPrinter::on_preset_loaded()
{
    // update the extruders count field
    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"));
    size_t extruders_count = nozzle_diameter->values.size();
    set_value("extruders_count", int(extruders_count));
    // update the GUI field according to the number of nozzle diameters supplied
    extruders_count_changed(extruders_count);

    //same for milling
    auto* milling_diameter = dynamic_cast<const ConfigOptionFloats*>(m_config->option("milling_diameter"));
    size_t milling_count = milling_diameter->values.size();
    set_value("milling_count", int(milling_count));
    milling_count_changed(milling_count);
}

void TabPrinter::update_pages()
{

    // update m_pages ONLY if printer technology is changed
    const PrinterTechnology new_printer_technology = m_presets->get_edited_preset().printer_technology();
    if (new_printer_technology == m_printer_technology)
        return;

    // hide all old pages
    for (auto& el : m_pages)
        el.get()->Hide();

    // set m_pages to m_pages_(technology before changing)
    m_printer_technology == ptFFF ? m_pages.swap(m_pages_fff) : m_pages.swap(m_pages_sla);

    // build Tab according to the technology, if it's not exist jet OR
    // set m_pages_(technology after changing) to m_pages
    // m_printer_technology will be set by Tab::load_current_preset()
    if (new_printer_technology == ptFFF)
    {
        if (m_pages_fff.empty())
        {
            build_fff();
            if (m_extruders_count > 1)
            {
                m_preset_bundle->update_multi_material_filament_presets();
                on_value_change("extruders_count", (int)m_extruders_count);
            }
        }
        else
            m_pages.swap(m_pages_fff);

         wxGetApp().sidebar().update_objects_list_extruder_column(m_extruders_count);
    }
    else
        m_pages_sla.empty() ? build_sla() : m_pages.swap(m_pages_sla);

    rebuild_page_tree();
}

void TabPrinter::update()
{
    m_update_cnt++;
    m_presets->get_edited_preset().printer_technology() == ptFFF ? update_fff() : update_sla();
    m_update_cnt--;

    if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabPrinter::update_fff()
{
//	Freeze();

    bool en;
    Field* field = get_field("serial_speed");
    if (field) {
        en = !m_config->opt_string("serial_port").empty();
        field = get_field("serial_speed");
        if (field)
            field->toggle(en);
        if (m_config->opt_int("serial_speed") != 0 && en)
            m_serial_test_btn->Enable();
        else
            m_serial_test_btn->Disable();
    }

    {
        std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
        if(m_print_host_test_btn)
            m_print_host_test_btn->Enable(!m_config->opt_string("print_host").empty() && host->can_test());
        if(m_printhost_browse_btn)
            m_printhost_browse_btn->Enable(host->has_auto_discovery());
    }

    bool have_multiple_extruders = m_extruders_count > 1;
    field = get_field("toolchange_gcode");
    if (field)
        field->toggle(have_multiple_extruders);
    field = get_field("single_extruder_multi_material");
    if (field)
        field->toggle(have_multiple_extruders);

    bool is_marlin_flavor = m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value == gcfMarlin;

    {
        Field *sm = get_field("silent_mode");
        if (sm) {
            if (!is_marlin_flavor) {
                // Disable silent mode for non-marlin firmwares.
                sm->toggle(false);
            }
            if (is_marlin_flavor)
                sm->enable();
            else
                sm->disable();
        }
    }
    if (m_config->option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor")->value == gcfKlipper)
        GCodeWriter::PausePrintCode = "PAUSE";
    else 
        GCodeWriter::PausePrintCode = "M601";

    if (m_is_marlin != is_marlin_flavor) {
        m_is_marlin = is_marlin_flavor;
        m_rebuild_kinematics_page = true;
    }

    if (m_use_silent_mode != m_config->opt_bool("silent_mode")) {
        m_rebuild_kinematics_page = true;
        m_use_silent_mode = m_config->opt_bool("silent_mode");
    }

    for (size_t i = 0; i < m_extruders_count; ++i) {
        bool have_retract_length = m_config->opt_float("retract_length", i) > 0;

        // when using firmware retraction, firmware decides retraction length
        bool use_firmware_retraction = m_config->opt_bool("use_firmware_retraction");
        field = get_field("retract_length", i);
        if(field)
            field->toggle(!use_firmware_retraction);

        // user can customize travel length if we have retraction length or we"re using
        // firmware retraction
        field = get_field("retract_before_travel", i);
        if (field)
            field->toggle(have_retract_length || use_firmware_retraction);

        // user can customize other retraction options if retraction is enabled
        bool retraction = (have_retract_length || use_firmware_retraction);
        std::vector<std::string> vec = { "retract_lift", "retract_layer_change" };
        for (auto el : vec) {
            field = get_field(el, i);
            if (field)
                field->toggle(retraction);
        }

        // retract lift above / below only applies if using retract lift
        vec.resize(0);
        vec = { "retract_lift_above", "retract_lift_below", "retract_lift_not_last_layer" };
        for (auto el : vec) {
            field = get_field(el, i);
            if (field)
                field->toggle(retraction && m_config->opt_float("retract_lift", i) > 0);
        }

        // some options only apply when not using firmware retraction
        vec.resize(0);
        vec = { "retract_speed", "deretract_speed", "retract_before_wipe", "retract_restart_extra", "wipe" };
        for (auto el : vec) {
            field = get_field(el, i);
            if (field)
                field->toggle(retraction && !use_firmware_retraction);
        }

        bool wipe = m_config->opt_bool("wipe", i);
        field = get_field("retract_before_wipe", i);
        if (field)
            field->toggle(wipe);

        if (use_firmware_retraction && wipe) {
            wxMessageDialog dialog(parent(),
                _(L("The Wipe option is not available when using the Firmware Retraction mode.\n"
                "\nShall I disable it in order to enable Firmware Retraction?")),
                _(L("Firmware Retraction")), wxICON_WARNING | wxYES | wxNO);

            DynamicPrintConfig new_conf = *m_config;
            if (dialog.ShowModal() == wxID_YES) {
                auto wipe = static_cast<ConfigOptionBools*>(m_config->option("wipe")->clone());
                for (size_t w = 0; w < wipe->values.size(); w++)
                    wipe->values[w] = false;
                new_conf.set_key_value("wipe", wipe);
            }
            else {
                new_conf.set_key_value("use_firmware_retraction", new ConfigOptionBool(false));
            }
            load_config(new_conf);
        }

        field = get_field("retract_length_toolchange", i);
        if (field)
            field->toggle(have_multiple_extruders);

        bool toolchange_retraction = m_config->opt_float("retract_length_toolchange", i) > 0;
        field = get_field("retract_restart_extra_toolchange", i);
        if (field)
            field->toggle(have_multiple_extruders && toolchange_retraction);
    }
    if (m_has_single_extruder_MM_page) {
        bool have_advanced_wipe_volume = m_config->opt_bool("wipe_advanced");
        for (auto el : { "wipe_advanced_nozzle_melted_volume", "wipe_advanced_multiplier", "wipe_advanced_algo" }) {
            Field *field = get_field(el);
            if (field)
                field->toggle(have_advanced_wipe_volume);
        }
    }

    //z step checks
    {
        double z_step = m_config->opt_float("z_step");
        DynamicPrintConfig new_conf;
        bool has_changed = false;
        const std::vector<double>& min_layer_height = m_config->option<ConfigOptionFloats>("min_layer_height")->values;
        for (int i = 0; i < min_layer_height.size(); i++)
            if (min_layer_height[i] / z_step != 0) {
                if(!has_changed )
                    new_conf = *m_config;
                new_conf.option<ConfigOptionFloats>("min_layer_height")->values[i] = std::max(z_step, Slic3r::check_z_step(new_conf.option<ConfigOptionFloats>("min_layer_height")->values[i], z_step));
                has_changed = true;
            }
        const std::vector<double>& max_layer_height = m_config->option<ConfigOptionFloats>("max_layer_height")->values;
        for (int i = 0; i < max_layer_height.size(); i++)
            if (max_layer_height[i] / z_step != 0) {
                if (!has_changed)
                    new_conf = *m_config;
                new_conf.option<ConfigOptionFloats>("max_layer_height")->values[i] = std::max(z_step, Slic3r::check_z_step(new_conf.option<ConfigOptionFloats>("max_layer_height")->values[i], z_step));
                has_changed = true;
            }
        if (has_changed) {
            load_config(new_conf);
        }
    }

//    Thaw();
}

void TabPrinter::update_sla()
{ ; }

void Tab::update_ui_items_related_on_parent_preset(const Preset* selected_preset_parent)
{
    m_is_default_preset = selected_preset_parent != nullptr && selected_preset_parent->is_default;

    m_bmp_non_system = selected_preset_parent ? &m_bmp_value_unlock : &m_bmp_white_bullet;
    m_ttg_non_system = selected_preset_parent ? &m_ttg_value_unlock : &m_ttg_white_bullet_ns;
    m_tt_non_system  = selected_preset_parent ? &m_tt_value_unlock  : &m_ttg_white_bullet_ns;
}

// Initialize the UI from the current preset
void Tab::load_current_preset()
{
    const Preset& preset = m_presets->get_edited_preset();

    (preset.is_default || preset.is_system) ? m_btn_delete_preset->Disable() : m_btn_delete_preset->Enable(true);

    update();
    if (m_type == Slic3r::Preset::TYPE_PRINTER) {
        // For the printer profile, generate the extruder pages.
        if (preset.printer_technology() == ptFFF)
            on_preset_loaded();
        else
            wxGetApp().sidebar().update_objects_list_extruder_column(1);
    }
    // Reload preset pages with the new configuration values.
    reload_config();

    update_ui_items_related_on_parent_preset(m_presets->get_selected_preset_parent());

//	m_undo_to_sys_btn->Enable(!preset.is_default);

#if 0
    // use CallAfter because some field triggers schedule on_change calls using CallAfter,
    // and we don't want them to be called after this update_dirty() as they would mark the
    // preset dirty again
    // (not sure this is true anymore now that update_dirty is idempotent)
    wxTheApp->CallAfter([this]
#endif
    {
        // checking out if this Tab exists till this moment
        if (!wxGetApp().checked_tab(this))
            return;
        update_tab_ui();

        // update show/hide tabs
        if (m_type == Slic3r::Preset::TYPE_PRINTER) {
            const PrinterTechnology printer_technology = m_presets->get_edited_preset().printer_technology();
            if (printer_technology != static_cast<TabPrinter*>(this)->m_printer_technology)
            {
                for (auto tab : wxGetApp().tabs_list) {
                    if (tab->type() == Preset::TYPE_PRINTER) // Printer tab is shown every time
                        continue;
                    if (tab->supports_printer_technology(printer_technology))
                    {
                        wxGetApp().tab_panel()->InsertPage(wxGetApp().tab_panel()->FindPage(this), tab, tab->title());
                        #ifdef __linux__ // the tabs apparently need to be explicitly shown on Linux (pull request #1563)
                            int page_id = wxGetApp().tab_panel()->FindPage(tab);
                            wxGetApp().tab_panel()->GetPage(page_id)->Show(true);
                        #endif // __linux__
                    }
                    else {
                        int page_id = wxGetApp().tab_panel()->FindPage(tab);
                        wxGetApp().tab_panel()->GetPage(page_id)->Show(false);
                        wxGetApp().tab_panel()->RemovePage(page_id);
                    }
                }
                static_cast<TabPrinter*>(this)->m_printer_technology = printer_technology;
            }
            on_presets_changed();
            if (printer_technology == ptFFF) {
                static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<TabPrinter*>(this)->m_extruders_count;
                const Preset* parent_preset = m_presets->get_selected_preset_parent();
                static_cast<TabPrinter*>(this)->m_sys_extruders_count = parent_preset == nullptr ? 0 :
                    static_cast<const ConfigOptionFloats*>(parent_preset->config.option("nozzle_diameter"))->values.size();
                static_cast<TabPrinter*>(this)->m_initial_milling_count = static_cast<TabPrinter*>(this)->m_milling_count;
                static_cast<TabPrinter*>(this)->m_sys_milling_count = parent_preset == nullptr ? 0 :
                    static_cast<const ConfigOptionFloats*>(parent_preset->config.option("milling_diameter"))->values.size();
            }
        }
        else {
            on_presets_changed();
            if (m_type == Preset::TYPE_SLA_PRINT || m_type == Preset::TYPE_PRINT)
                update_frequently_changed_parameters();
        }

        m_opt_status_value = (m_presets->get_selected_preset_parent() ? osSystemValue : 0) | osInitValue;
        init_options_list();
        update_visibility();
        update_changed_ui();
    }
#if 0
    );
#endif
}

//Regerenerate content of the page tree.
void Tab::rebuild_page_tree()
{
    // get label of the currently selected item
    const auto sel_item = m_treectrl->GetSelection();
    const auto selected = sel_item ? m_treectrl->GetItemText(sel_item) : "";
    const auto rootItem = m_treectrl->GetRootItem();

    auto have_selection = 0;
    m_treectrl->DeleteChildren(rootItem);
    for (auto p : m_pages)
    {
        auto itemId = m_treectrl->AppendItem(rootItem, p->title(), p->iconID());
        m_treectrl->SetItemTextColour(itemId, p->get_item_colour());
        if (p->title() == selected) {
            m_treectrl->SelectItem(itemId);
            have_selection = 1;
        }
    }

    if (!have_selection) {
        // this is triggered on first load, so we don't disable the sel change event
        auto item = m_treectrl->GetFirstVisibleItem();
        if (item) {
            m_treectrl->SelectItem(item);
        }
    }
}

void Tab::update_page_tree_visibility()
{
    const auto sel_item = m_treectrl->GetSelection();
    const auto selected = sel_item ? m_treectrl->GetItemText(sel_item) : "";
    const auto rootItem = m_treectrl->GetRootItem();

    auto have_selection = 0;
    m_treectrl->DeleteChildren(rootItem);
    for (auto p : m_pages)
    {
        if (!p->get_show())
            continue;
        auto itemId = m_treectrl->AppendItem(rootItem, p->title(), p->iconID());
        m_treectrl->SetItemTextColour(itemId, p->get_item_colour());
        if (p->title() == selected) {
            m_treectrl->SelectItem(itemId);
            have_selection = 1;
        }
    }

    if (!have_selection) {
        // this is triggered on first load, so we don't disable the sel change event
        auto item = m_treectrl->GetFirstVisibleItem();
        if (item) {
            m_treectrl->SelectItem(item);
        }
    }

}

// Called by the UI combo box when the user switches profiles, and also to delete the current profile.
// Select a preset by a name.If !defined(name), then the default preset is selected.
// If the current profile is modified, user is asked to save the changes.
void Tab::select_preset(std::string preset_name, bool delete_current)
{
    if (preset_name.empty()) {
        if (delete_current) {
            // Find an alternate preset to be selected after the current preset is deleted.
            const std::deque<Preset> &presets 		= this->m_presets->get_presets();
            size_t    				  idx_current   = this->m_presets->get_idx_selected();
            // Find the next visible preset.
            size_t 				      idx_new       = idx_current + 1;
            if (idx_new < presets.size())
                for (; idx_new < presets.size() && ! presets[idx_new].is_visible; ++ idx_new) ;
            if (idx_new == presets.size())
                for (idx_new = idx_current - 1; idx_new > 0 && ! presets[idx_new].is_visible; -- idx_new);
            preset_name = presets[idx_new].name;
        } else {
            // If no name is provided, select the "-- default --" preset.
            preset_name = m_presets->default_preset().name;
        }
    }
    assert(! delete_current || (m_presets->get_edited_preset().name != preset_name && m_presets->get_edited_preset().is_user()));
    bool current_dirty = ! delete_current && m_presets->current_is_dirty();
    bool print_tab     = m_presets->type() == Preset::TYPE_PRINT || m_presets->type() == Preset::TYPE_SLA_PRINT;
    bool printer_tab   = m_presets->type() == Preset::TYPE_PRINTER;
    bool canceled      = false;
    bool technology_changed = false;
    m_dependent_tabs.clear();
    if (current_dirty && ! may_discard_current_dirty_preset()) {
        canceled = true;
    } else if (print_tab) {
        // Before switching the print profile to a new one, verify, whether the currently active filament or SLA material
        // are compatible with the new print.
        // If it is not compatible and the current filament or SLA material are dirty, let user decide
        // whether to discard the changes or keep the current print selection.
        PresetWithVendorProfile printer_profile = m_preset_bundle->printers.get_edited_preset_with_vendor_profile();
        PrinterTechnology  printer_technology = printer_profile.preset.printer_technology();
        PresetCollection  &dependent = (printer_technology == ptFFF) ? m_preset_bundle->filaments : m_preset_bundle->sla_materials;
        bool 			   old_preset_dirty = dependent.current_is_dirty();
        bool 			   new_preset_compatible = is_compatible_with_print(dependent.get_edited_preset_with_vendor_profile(), 
        	m_presets->get_preset_with_vendor_profile(*m_presets->find_preset(preset_name, true)), printer_profile);
        if (! canceled)
            canceled = old_preset_dirty && ! new_preset_compatible && ! may_discard_current_dirty_preset(&dependent, preset_name);
        if (! canceled) {
            // The preset will be switched to a different, compatible preset, or the '-- default --'.
            m_dependent_tabs.emplace_back((printer_technology == ptFFF) ? Preset::Type::TYPE_FILAMENT : Preset::Type::TYPE_SLA_MATERIAL);
            if (old_preset_dirty && ! new_preset_compatible)
                dependent.discard_current_changes();
        }
    } else if (printer_tab) {
        // Before switching the printer to a new one, verify, whether the currently active print and filament
        // are compatible with the new printer.
        // If they are not compatible and the current print or filament are dirty, let user decide
        // whether to discard the changes or keep the current printer selection.
        //
        // With the introduction of the SLA printer types, we need to support switching between
        // the FFF and SLA printers.
        const Preset 		&new_printer_preset     = *m_presets->find_preset(preset_name, true);
		const PresetWithVendorProfile new_printer_preset_with_vendor_profile = m_presets->get_preset_with_vendor_profile(new_printer_preset);
        PrinterTechnology    old_printer_technology = m_presets->get_edited_preset().printer_technology();
        PrinterTechnology    new_printer_technology = new_printer_preset.printer_technology();
        if (new_printer_technology == ptSLA && old_printer_technology == ptFFF && !may_switch_to_SLA_preset())
            canceled = true;
        else {
            struct PresetUpdate {
                Preset::Type         tab_type;
                PresetCollection 	*presets;
                PrinterTechnology    technology;
                bool    	         old_preset_dirty;
                bool         	     new_preset_compatible;
            };
            std::vector<PresetUpdate> updates = {
                { Preset::Type::TYPE_PRINT,         &m_preset_bundle->prints,       ptFFF },
                { Preset::Type::TYPE_SLA_PRINT,     &m_preset_bundle->sla_prints,   ptSLA },
                { Preset::Type::TYPE_FILAMENT,      &m_preset_bundle->filaments,    ptFFF },
                { Preset::Type::TYPE_SLA_MATERIAL,  &m_preset_bundle->sla_materials,ptSLA }
            };
            for (PresetUpdate &pu : updates) {
                pu.old_preset_dirty = (old_printer_technology == pu.technology) && pu.presets->current_is_dirty();
                pu.new_preset_compatible = (new_printer_technology == pu.technology) && is_compatible_with_printer(pu.presets->get_edited_preset_with_vendor_profile(), new_printer_preset_with_vendor_profile);
                if (!canceled)
                    canceled = pu.old_preset_dirty && !pu.new_preset_compatible && !may_discard_current_dirty_preset(pu.presets, preset_name);
            }
            if (!canceled) {
                for (PresetUpdate &pu : updates) {
                    // The preset will be switched to a different, compatible preset, or the '-- default --'.
                    if (pu.technology == new_printer_technology)
                        m_dependent_tabs.emplace_back(pu.tab_type);
                    if (pu.old_preset_dirty && !pu.new_preset_compatible)
                        pu.presets->discard_current_changes();
                }
            }
        }
        if (! canceled)
        	technology_changed = old_printer_technology != new_printer_technology;
    }

    if (! canceled && delete_current) {
        // Delete the file and select some other reasonable preset.
        // It does not matter which preset will be made active as the preset will be re-selected from the preset_name variable.
        // The 'external' presets will only be removed from the preset list, their files will not be deleted.
        try {
            m_presets->delete_current_preset();
        } catch (const std::exception & /* e */) {
            //FIXME add some error reporting!
            canceled = true;
        }
    }

    if (canceled) {
        update_tab_ui();
        // Trigger the on_presets_changed event so that we also restore the previous value in the plater selector,
        // if this action was initiated from the plater.
        on_presets_changed();
    } else {
        if (current_dirty)
            m_presets->discard_current_changes();

        const bool is_selected = m_presets->select_preset_by_name(preset_name, false) || delete_current;
        assert(m_presets->get_edited_preset().name == preset_name || ! is_selected);
        // Mark the print & filament enabled if they are compatible with the currently selected preset.
        // The following method should not discard changes of current print or filament presets on change of a printer profile,
        // if they are compatible with the current printer.
        auto update_compatible_type = [delete_current](bool technology_changed, bool on_page, bool show_incompatible_presets) {
        	return (delete_current || technology_changed) ? PresetSelectCompatibleType::Always :
        	       on_page                                ? PresetSelectCompatibleType::Never  :
        	       show_incompatible_presets              ? PresetSelectCompatibleType::OnlyIfWasCompatible : PresetSelectCompatibleType::Always;
        };
        if (current_dirty || delete_current || print_tab || printer_tab)
            m_preset_bundle->update_compatible(
            	update_compatible_type(technology_changed, print_tab,   (print_tab ? this : wxGetApp().get_tab(Preset::TYPE_PRINT))->m_show_incompatible_presets),
            	update_compatible_type(technology_changed, false, 		wxGetApp().get_tab(Preset::TYPE_FILAMENT)->m_show_incompatible_presets));
        // Initialize the UI from the current preset.
        if (printer_tab)
            static_cast<TabPrinter*>(this)->update_pages();

        if (! is_selected && printer_tab)
        {
            /* There is a case, when :
             * after Config Wizard applying we try to select previously selected preset, but
             * in a current configuration this one:
             *  1. doesn't exist now,
             *  2. have another printer_technology
             * So, it is necessary to update list of dependent tabs
             * to the corresponding printer_technology
             */
            const PrinterTechnology printer_technology = m_presets->get_edited_preset().printer_technology();
            if (printer_technology == ptFFF && m_dependent_tabs.front() != Preset::Type::TYPE_PRINT)
                m_dependent_tabs = { Preset::Type::TYPE_PRINT, Preset::Type::TYPE_FILAMENT };
            else if (printer_technology == ptSLA && m_dependent_tabs.front() != Preset::Type::TYPE_SLA_PRINT)
                m_dependent_tabs = { Preset::Type::TYPE_SLA_PRINT, Preset::Type::TYPE_SLA_MATERIAL };
        }
        load_current_preset();
    }
}

// If the current preset is dirty, the user is asked whether the changes may be discarded.
// if the current preset was not dirty, or the user agreed to discard the changes, 1 is returned.
bool Tab::may_discard_current_dirty_preset(PresetCollection* presets /*= nullptr*/, const std::string& new_printer_name /*= ""*/)
{
    if (presets == nullptr) presets = m_presets;
    // Display a dialog showing the dirty options in a human readable form.
    const Preset& old_preset = presets->get_edited_preset();
    std::string   type_name  = presets->name();
    wxString      tab        = "          ";
    wxString      name       = old_preset.is_default ?
        from_u8((boost::format(_utf8(L("Default preset (%s)"))) % _utf8(type_name)).str()) :
        from_u8((boost::format(_utf8(L("Preset (%s)"))) % _utf8(type_name)).str()) + "\n" + tab + old_preset.name;

    // Collect descriptions of the dirty options.
    wxString changes;
    for (const std::string &opt_key : presets->current_dirty_options()) {
        const ConfigOptionDef &opt = m_config->def()->options.at(opt_key);
        /*std::string*/wxString name = "";
        if (opt.category != OptionCategory::none)
            name += _(toString(opt.category)) + " > ";
        name += opt.get_full_label();
        changes += tab + /*from_u8*/(name) + "\n";
    }
    // Show a confirmation dialog with the list of dirty options.
    wxString message = name + "\n\n";
    if (new_printer_name.empty())
        message += _(L("has the following unsaved changes:"));
    else {
        message += (m_type == Slic3r::Preset::TYPE_PRINTER) ?
                _(L("is not compatible with printer")) :
                _(L("is not compatible with print profile"));
        message += wxString("\n") + tab + from_u8(new_printer_name) + "\n\n";
        message += _(L("and it has the following unsaved changes:"));
    }
    wxMessageDialog confirm(parent(),
        message + "\n" + changes + "\n\n" + _(L("Discard changes and continue anyway?")),
        _(L("Unsaved Changes")), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION);
    return confirm.ShowModal() == wxID_YES;
}

// If we are switching from the FFF-preset to the SLA, we should to control the printed objects if they have a part(s).
// Because of we can't to print the multi-part objects with SLA technology.
bool Tab::may_switch_to_SLA_preset()
{
    if (model_has_multi_part_objects(wxGetApp().model()))
    {
        show_info( parent(),
                    _(L("It's impossible to print multi-part object(s) with SLA technology.")) + "\n\n" +
                    _(L("Please check your object list before preset changing.")),
                    _(L("Attention!")) );
        return false;
    }
    return true;
}

void Tab::OnTreeSelChange(wxTreeEvent& event)
{
    if (m_disable_tree_sel_changed_event)
        return;

// There is a bug related to Ubuntu overlay scrollbars, see https://github.com/prusa3d/PrusaSlicer/issues/898 and https://github.com/prusa3d/PrusaSlicer/issues/952.
// The issue apparently manifests when Show()ing a window with overlay scrollbars while the UI is frozen. For this reason,
// we will Thaw the UI prematurely on Linux. This means destroing the no_updates object prematurely.
#ifdef __linux__
    std::unique_ptr<wxWindowUpdateLocker> no_updates(new wxWindowUpdateLocker(this));
#else
    /* On Windows we use DoubleBuffering during rendering,
     * so on Window is no needed to call a Freeze/Thaw functions.
     * But under OSX (builds compiled with MacOSX10.14.sdk) wxStaticBitmap rendering is broken without Freeze/Thaw call.
     */
#ifdef __WXOSX__
	wxWindowUpdateLocker noUpdates(this);
#endif
#endif

    if (m_pages.empty())
        return;

    Page* page = nullptr;
    const auto sel_item = m_treectrl->GetSelection();
    const auto selection = sel_item ? m_treectrl->GetItemText(sel_item) : "";
    for (auto p : m_pages)
        if (p->title() == selection)
        {
            page = p.get();
            m_is_nonsys_values = page->m_is_nonsys_values;
            m_is_modified_values = page->m_is_modified_values;
            break;
        }
    if (page == nullptr) return;

    for (auto& el : m_pages)
//		if (el.get()->IsShown()) {
            el.get()->Hide();
//			break;
//		}

    #ifdef __linux__
        no_updates.reset(nullptr);
    #endif

    update_undo_buttons();
    page->Show();
//	if (! page->layout_valid) {
        page->layout_valid = true;
        m_hsizer->Layout();
        Refresh();
//	}
}

void Tab::OnKeyDown(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_TAB)
        m_treectrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
    else
        event.Skip();
}

// Save the current preset into file.
// This removes the "dirty" flag of the preset, possibly creates a new preset under a new name,
// and activates the new preset.
// Wizard calls save_preset with a name "My Settings", otherwise no name is provided and this method
// opens a Slic3r::GUI::SavePresetWindow dialog.
void Tab::save_preset(std::string name /*= ""*/, bool detach)
{
    // since buttons(and choices too) don't get focus on Mac, we set focus manually
    // to the treectrl so that the EVT_* events are fired for the input field having
    // focus currently.is there anything better than this ?
//!	m_treectrl->OnSetFocus();

    std::string suffix = detach ? _utf8(L("Detached")) : _CTX_utf8(L_CONTEXT("Copy", "PresetName"), "PresetName");

    if (name.empty()) {
        const Preset &preset = m_presets->get_selected_preset();
        auto default_name = preset.is_default ? "Untitled" :
//                            preset.is_system ? (boost::format(_CTX_utf8(L_CONTEXT("%1% - Copy", "PresetName"), "PresetName")) % preset.name).str() :
                            preset.is_system ? (boost::format(("%1% - %2%")) % preset.name % suffix).str() :
                            preset.name;

        bool have_extention = boost::iends_with(default_name, ".ini");
        if (have_extention) {
            size_t len = default_name.length()-4;
            default_name.resize(len);
        }
        //[map $_->name, grep !$_->default && !$_->external, @{$self->{presets}}],
        std::vector<std::string> values;
        for (size_t i = 0; i < m_presets->size(); ++i) {
            const Preset &preset = m_presets->preset(i);
            if (preset.is_default || preset.is_system || preset.is_external)
                continue;
            values.push_back(preset.name);
        }

        SavePresetWindow dlg(parent());
        dlg.build(title(), default_name, values);
        auto result = dlg.ShowModal();
        // OK => ADD, APPLY => RENAME
        if (result != wxID_OK && result != wxID_APPLY)
            return;
        name = dlg.get_name();
        if (result == wxID_APPLY && preset.is_system) {
            show_error(this, _(L("Cannot modify a system profile name.")));
            return;
        }
        if (name == "") {
            show_error(this, _(L("The supplied name is empty. It can't be saved.")));
            return;
        }
        const Preset *existing = m_presets->find_preset(name, false);
        if (existing && (existing->is_default || existing->is_system)) {
            show_error(this, _(L("Cannot overwrite a system profile.")));
            return;
        }
        if (existing && (existing->is_external)) {
            show_error(this, _(L("Cannot overwrite an external profile.")));
            return;
        }
        if (existing && name != preset.name)
        {
            wxString msg_text = GUI::from_u8((boost::format(_utf8(L("Preset with name \"%1%\" already exists."))) % name).str());
            msg_text += "\n" + _(L("Replace?"));
            wxMessageDialog dialog(nullptr, msg_text, _(L("Warning")), wxICON_WARNING | wxYES | wxNO);

            if (dialog.ShowModal() == wxID_NO)
                return;

            // Remove the preset from the list.
            m_presets->delete_preset(name);
        }
        //if RENAME
        if (result == wxID_APPLY) {
            // Remove the old preset from the list.
            m_presets->delete_preset(preset.name);
        }
    }

    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    m_presets->save_current_preset(name, detach);
    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are no more compatible.
    m_preset_bundle->update_compatible(PresetSelectCompatibleType::Never);
    // Add the new item into the UI component, remove dirty flags and activate the saved item.
    update_tab_ui();
    // Update the selection boxes at the plater.
    on_presets_changed();
    // If current profile is saved, "delete preset" button have to be enabled
    m_btn_delete_preset->Enable(true);

    if (m_type == Preset::TYPE_PRINTER)
        static_cast<TabPrinter*>(this)->m_initial_extruders_count = static_cast<TabPrinter*>(this)->m_extruders_count;
    if (m_type == Preset::TYPE_PRINTER)
        static_cast<TabPrinter*>(this)->m_initial_milling_count = static_cast<TabPrinter*>(this)->m_milling_count;

    // Parent preset is "default" after detaching, so we should to update UI values, related on parent preset  
    if (detach)
        update_ui_items_related_on_parent_preset(m_presets->get_selected_preset_parent());

    update_changed_ui();

    /* If filament preset is saved for multi-material printer preset, 
     * there are cases when filament comboboxs are updated for old (non-modified) colors, 
     * but in full_config a filament_colors option aren't.*/
    if (m_type == Preset::TYPE_FILAMENT && wxGetApp().extruders_edited_cnt() > 1)
        wxGetApp().plater()->force_filament_colors_update();

    {
    	// Profile compatiblity is updated first when the profile is saved.
    	// Update profile selection combo boxes at the depending tabs to reflect modifications in profile compatibility.
	    std::vector<Preset::Type> dependent;
	    switch (m_type) {
	    case Preset::TYPE_PRINT:
	    	dependent = { Preset::TYPE_FILAMENT };
	    	break;
	    case Preset::TYPE_SLA_PRINT:
	    	dependent = { Preset::TYPE_SLA_MATERIAL };
	    	break;
	    case Preset::TYPE_PRINTER:
            if (static_cast<const TabPrinter*>(this)->m_printer_technology == ptFFF)
                dependent = { Preset::TYPE_PRINT, Preset::TYPE_FILAMENT };
            else
                dependent = { Preset::TYPE_SLA_PRINT, Preset::TYPE_SLA_MATERIAL };
	        break;
        default:
	        break;
	    }
	    for (Preset::Type preset_type : dependent)
			wxGetApp().get_tab(preset_type)->update_tab_ui();
	}
}

// Called for a currently selected preset.
void Tab::delete_preset()
{
    auto current_preset = m_presets->get_selected_preset();
    // Don't let the user delete the ' - default - ' configuration.
    std::string action = current_preset.is_external ? _utf8(L("remove")) : _utf8(L("delete"));
    // TRN  remove/delete
    const wxString msg = from_u8((boost::format(_utf8(L("Are you sure you want to %1% the selected preset?"))) % action).str());
    action = current_preset.is_external ? _utf8(L("Remove")) : _utf8(L("Delete"));
    // TRN  Remove/Delete
    wxString title = from_u8((boost::format(_utf8(L("%1% Preset"))) % action).str());  //action + _(L(" Preset"));
    if (current_preset.is_default ||
        wxID_YES != wxMessageDialog(parent(), msg, title, wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal())
        return;
    // Select will handle of the preset dependencies, of saving & closing the depending profiles, and
    // finally of deleting the preset.
    this->select_preset("", true);
}

void Tab::toggle_show_hide_incompatible()
{
    m_show_incompatible_presets = !m_show_incompatible_presets;
    update_show_hide_incompatible_button();
    update_tab_ui();
}

void Tab::update_show_hide_incompatible_button()
{
    m_btn_hide_incompatible_presets->SetBitmap_(m_show_incompatible_presets ?
        m_bmp_show_incompatible_presets : m_bmp_hide_incompatible_presets);
    m_btn_hide_incompatible_presets->SetToolTip(m_show_incompatible_presets ?
        "Both compatible an incompatible presets are shown. Click to hide presets not compatible with the current printer." :
        "Only compatible presets are shown. Click to show both the presets compatible and not compatible with the current printer.");
}

void Tab::update_ui_from_settings()
{
    // Show the 'show / hide presets' button only for the print and filament tabs, and only if enabled
    // in application preferences.
    m_show_btn_incompatible_presets = wxGetApp().app_config->get("show_incompatible_presets")[0] == '1' ? true : false;
    bool show = m_show_btn_incompatible_presets && m_type != Slic3r::Preset::TYPE_PRINTER;
    Layout();
    show ? m_btn_hide_incompatible_presets->Show() :  m_btn_hide_incompatible_presets->Hide();
    // If the 'show / hide presets' button is hidden, hide the incompatible presets.
    if (show) {
        update_show_hide_incompatible_button();
    }
    else {
        if (m_show_incompatible_presets) {
            m_show_incompatible_presets = false;
            update_tab_ui();
        }
    }
}

void Tab::create_line_with_widget(ConfigOptionsGroup* optgroup, const std::string& opt_key, widget_t widget)
{
    Line line = optgroup->create_single_option_line(opt_key);
    line.widget = widget;

    m_colored_Labels[opt_key] = nullptr;
    optgroup->append_line(line, &m_colored_Labels[opt_key]);
}

// Return a callback to create a Tab widget to mark the preferences as compatible / incompatible to the current printer.
wxSizer* Tab::compatible_widget_create(wxWindow* parent, PresetDependencies &deps)
{
    deps.checkbox = new wxCheckBox(parent, wxID_ANY, _(L("All")));
    deps.checkbox->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    add_scaled_button(parent, &deps.btn, "printer_white", from_u8((boost::format(" %s %s") % _utf8(L("Set")) % std::string(dots.ToUTF8())).str()), wxBU_LEFT | wxBU_EXACTFIT);
    deps.btn->SetFont(Slic3r::GUI::wxGetApp().normal_font());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add((deps.checkbox), 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add((deps.btn), 0, wxALIGN_CENTER_VERTICAL);

    deps.checkbox->Bind(wxEVT_CHECKBOX, ([this, &deps](wxCommandEvent e)
    {
        deps.btn->Enable(! deps.checkbox->GetValue());
        // All printers have been made compatible with this preset.
        if (deps.checkbox->GetValue())
            this->load_key_value(deps.key_list, std::vector<std::string> {});
        this->get_field(deps.key_condition)->toggle(deps.checkbox->GetValue());
        this->update_changed_ui();
    }) );

    deps.btn->Bind(wxEVT_BUTTON, ([this, parent, &deps](wxCommandEvent e)
    {
        // Collect names of non-default non-external profiles.
        PrinterTechnology printer_technology = m_preset_bundle->printers.get_edited_preset().printer_technology();
        PresetCollection &depending_presets  = (deps.type == Preset::TYPE_PRINTER) ? m_preset_bundle->printers :
                (printer_technology == ptFFF) ? m_preset_bundle->prints : m_preset_bundle->sla_prints;
        wxArrayString presets;
        for (size_t idx = 0; idx < depending_presets.size(); ++ idx)
        {
            Preset& preset = depending_presets.preset(idx);
            bool add = ! preset.is_default && ! preset.is_external;
            if (add && deps.type == Preset::TYPE_PRINTER)
                // Only add printers with the same technology as the active printer.
                add &= preset.printer_technology() == printer_technology;
            if (add)
                presets.Add(from_u8(preset.name));
        }

        wxMultiChoiceDialog dlg(parent, deps.dialog_title, deps.dialog_label, presets);
        // Collect and set indices of depending_presets marked as compatible.
        wxArrayInt selections;
        auto *compatible_printers = dynamic_cast<const ConfigOptionStrings*>(m_config->option(deps.key_list));
        if (compatible_printers != nullptr || !compatible_printers->values.empty())
            for (auto preset_name : compatible_printers->values)
                for (size_t idx = 0; idx < presets.GetCount(); ++idx)
                    if (presets[idx] == preset_name) {
                        selections.Add(idx);
                        break;
                    }
        dlg.SetSelections(selections);
        std::vector<std::string> value;
        // Show the dialog.
        if (dlg.ShowModal() == wxID_OK) {
            selections.Clear();
            selections = dlg.GetSelections();
            for (auto idx : selections)
                value.push_back(presets[idx].ToUTF8().data());
            if (value.empty()) {
                deps.checkbox->SetValue(1);
                deps.btn->Disable();
            }
            // All depending_presets have been made compatible with this preset.
            this->load_key_value(deps.key_list, value);
            this->update_changed_ui();
        }
    }));
    return sizer;
}

// Return a callback to create a TabPrinter widget to edit bed shape
wxSizer* TabPrinter::create_bed_shape_widget(wxWindow* parent)
{
    ScalableButton* btn;
    add_scaled_button(parent, &btn, "printer_white", " " + _(L("Set")) + " " + dots, wxBU_LEFT | wxBU_EXACTFIT);
    btn->SetFont(wxGetApp().normal_font());

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(btn);

    btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e)
        {
            BedShapeDialog dlg(this);
            dlg.build_dialog(*m_config->option<ConfigOptionPoints>("bed_shape"),
                *m_config->option<ConfigOptionString>("bed_custom_texture"),
                *m_config->option<ConfigOptionString>("bed_custom_model"));
            if (dlg.ShowModal() == wxID_OK) {
                const std::vector<Vec2d>& shape = dlg.get_shape();
                const std::string& custom_texture = dlg.get_custom_texture();
                const std::string& custom_model = dlg.get_custom_model();
                if (!shape.empty())
                {
                    load_key_value("bed_shape", shape);
                    load_key_value("bed_custom_texture", custom_texture);
                    load_key_value("bed_custom_model", custom_model);
                    update_changed_ui();
                }
            }
        }));

    return sizer;
}

void Tab::compatible_widget_reload(PresetDependencies &deps)
{
    if (deps.btn == nullptr) return; // check if it has been initalised (should be, but someone may want to remove it from the ui)
    bool has_any = ! m_config->option<ConfigOptionStrings>(deps.key_list)->values.empty();
    has_any ? deps.btn->Enable() : deps.btn->Disable();
    deps.checkbox->SetValue(! has_any);
    this->get_field(deps.key_condition)->toggle(! has_any);
}

void Tab::fill_icon_descriptions()
{
    m_icon_descriptions.emplace_back(&m_bmp_value_lock, L("LOCKED LOCK"),
        // TRN Description for "LOCKED LOCK"
        L("indicates that the settings are the same as the system (or default) values for the current option group"));

    m_icon_descriptions.emplace_back(&m_bmp_value_unlock, L("UNLOCKED LOCK"),
        // TRN Description for "UNLOCKED LOCK"
        L("indicates that some settings were changed and are not equal to the system (or default) values for "
        "the current option group.\n"
        "Click the UNLOCKED LOCK icon to reset all settings for current option group to "
        "the system (or default) values."));

    m_icon_descriptions.emplace_back(&m_bmp_white_bullet, L("WHITE BULLET"),
        // TRN Description for "WHITE BULLET"
        L("for the left button: indicates a non-system (or non-default) preset,\n"
          "for the right button: indicates that the settings hasn't been modified."));

    m_icon_descriptions.emplace_back(&m_bmp_value_revert, L("BACK ARROW"),
        // TRN Description for "BACK ARROW"
        L("indicates that the settings were changed and are not equal to the last saved preset for "
        "the current option group.\n"
        "Click the BACK ARROW icon to reset all settings for the current option group to "
        "the last saved preset."));
}

void Tab::set_tooltips_text()
{
    // --- Tooltip text for reset buttons (for whole options group)
    // Text to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    m_ttg_value_lock =		_(L("LOCKED LOCK icon indicates that the settings are the same as the system (or default) values "
                                "for the current option group"));
    m_ttg_value_unlock =	_(L("UNLOCKED LOCK icon indicates that some settings were changed and are not equal "
                                "to the system (or default) values for the current option group.\n"
                                "Click to reset all settings for current option group to the system (or default) values."));
    m_ttg_white_bullet_ns =	_(L("WHITE BULLET icon indicates a non system (or non default) preset."));
    m_ttg_non_system =		&m_ttg_white_bullet_ns;
    // Text to be shown on the "Undo user changes" button next to each input field.
    m_ttg_white_bullet =	_(L("WHITE BULLET icon indicates that the settings are the same as in the last saved "
                                "preset for the current option group."));
    m_ttg_value_revert =	_(L("BACK ARROW icon indicates that the settings were changed and are not equal to "
                                "the last saved preset for the current option group.\n"
                                "Click to reset all settings for the current option group to the last saved preset."));

    // --- Tooltip text for reset buttons (for each option in group)
    // Text to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
    m_tt_value_lock =		_(L("LOCKED LOCK icon indicates that the value is the same as the system (or default) value."));
    m_tt_value_unlock =		_(L("UNLOCKED LOCK icon indicates that the value was changed and is not equal "
                                "to the system (or default) value.\n"
                                "Click to reset current value to the system (or default) value."));
    // 	m_tt_white_bullet_ns=	_(L("WHITE BULLET icon indicates a non system preset."));
    m_tt_non_system =		&m_ttg_white_bullet_ns;
    // Text to be shown on the "Undo user changes" button next to each input field.
    m_tt_white_bullet =		_(L("WHITE BULLET icon indicates that the value is the same as in the last saved preset."));
    m_tt_value_revert =		_(L("BACK ARROW icon indicates that the value was changed and is not equal to the last saved preset.\n"
                                "Click to reset current value to the last saved preset."));
}

void Page::reload_config()
{
    for (auto group : m_optgroups)
        group->reload_config();
}

void Page::update_visibility(ConfigOptionMode mode)
{
    bool ret_val = false;
    for (auto group : m_optgroups)
        ret_val = group->update_visibility(mode) || ret_val;

    m_show = ret_val;
}

void Page::msw_rescale()
{
    for (auto group : m_optgroups)
        group->msw_rescale();
}

Field* Page::get_field(const t_config_option_key& opt_key, int opt_index /*= -1*/) const
{
    Field* field = nullptr;
    for (auto opt : m_optgroups) {
        field = opt->get_fieldc(opt_key, opt_index);
        if (field != nullptr)
            return field;
    }
    return field;
}

bool Page::set_value(const t_config_option_key& opt_key, const boost::any& value) {
    bool changed = false;
    for(auto optgroup: m_optgroups) {
        if (optgroup->set_value(opt_key, value))
            changed = 1 ;
    }
    return changed;
}

// package Slic3r::GUI::Tab::Page;
ConfigOptionsGroupShp Page::new_optgroup(const wxString& title, int noncommon_title_width /*= -1*/)
{
    auto extra_column = [this](wxWindow* parent, const Line& line)
    {
        std::string bmp_name;
        const std::vector<Option>& options = line.get_options();
        int mode_id = int(options[0].opt.mode);
        const wxBitmap& bitmap = options.size() == 0 || options[0].opt.gui_type == "legend" ? wxNullBitmap :
                                 m_mode_bitmap_cache[mode_id].bmp();
        auto bmp = new wxStaticBitmap(parent, wxID_ANY, bitmap);
        bmp->SetClientData((void*)&m_mode_bitmap_cache[mode_id]);

        bmp->SetBackgroundStyle(wxBG_STYLE_PAINT);
        return bmp;
    };

    //! config_ have to be "right"
    ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(this, title, m_config, true, extra_column);
    if (noncommon_title_width >= 0)
        optgroup->title_width = noncommon_title_width;

#ifdef __WXOSX__
        auto tab = GetParent()->GetParent();
#else
        auto tab = GetParent();
#endif
    optgroup->m_on_change = [this, tab](t_config_option_key opt_key, boost::any value) {
        //! This function will be called from OptionGroup.
        //! Using of CallAfter is redundant.
        //! And in some cases it causes update() function to be recalled again
//!        wxTheApp->CallAfter([this, opt_key, value]() {
            static_cast<Tab*>(tab)->update_dirty();
            static_cast<Tab*>(tab)->on_value_change(opt_key, value);
//!        });
    };

    optgroup->m_get_initial_config = [this, tab]() {
        DynamicPrintConfig config = static_cast<Tab*>(tab)->m_presets->get_selected_preset().config;
        return config;
    };

    optgroup->m_get_sys_config = [this, tab]() {
        DynamicPrintConfig config = static_cast<Tab*>(tab)->m_presets->get_selected_preset_parent()->config;
        return config;
    };

    optgroup->have_sys_config = [this, tab]() {
        return static_cast<Tab*>(tab)->m_presets->get_selected_preset_parent() != nullptr;
    };

    optgroup->rescale_extra_column_item = [this](wxWindow* win) {
        auto *ctrl = dynamic_cast<wxStaticBitmap*>(win);
        if (ctrl == nullptr)
            return;

        ctrl->SetBitmap(reinterpret_cast<ScalableBitmap*>(ctrl->GetClientData())->bmp());
    };

    vsizer()->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 10);
    m_optgroups.push_back(optgroup);

    return optgroup;
}

void SavePresetWindow::build(const wxString& title, const std::string& default_name, std::vector<std::string> &values)
{
    // TRN Preset
    auto text = new wxStaticText(this, wxID_ANY, from_u8((boost::format(_utf8(L("Save %s as:"))) % into_u8(title)).str()),
                                    wxDefaultPosition, wxDefaultSize);
    m_combo = new wxComboBox(this, wxID_ANY, from_u8(default_name),
                            wxDefaultPosition, wxSize(35 * wxGetApp().em_unit(), -1), 0, 0, wxTE_PROCESS_ENTER);
    

    for (auto value : values)
        m_combo->Append(from_u8(value));
    wxStdDialogButtonSizer * buttons = CreateStdDialogButtonSizer(wxOK | wxNO | wxCANCEL);

    auto sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 0, wxEXPAND | wxALL, 10);
    sizer->Add(m_combo, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
    sizer->Add(buttons, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);

    //rename no in rename and add callback
    wxButton* btn_no = static_cast<wxButton*>(FindWindowById(wxID_NO, this));
    btn_no->SetLabel(_utf8(L("Rename")));
    btn_no->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { accept(true); });

    wxButton* btn = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
    btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { accept(); });
    m_combo->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { accept(); });

    SetSizer(sizer);
    sizer->SetSizeHints(this);
}

void SavePresetWindow::accept(bool rename)
{
    m_chosen_name = normalize_utf8_nfc(m_combo->GetValue().ToUTF8());
    if (!m_chosen_name.empty()) {
        const char* unusable_symbols = "<>[]:/\\|?*\"";
        bool is_unusable_symbol = false;
        bool is_unusable_suffix = false;
        const std::string unusable_suffix = PresetCollection::get_suffix_modified();//"(modified)";
        for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
            if (m_chosen_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
                is_unusable_symbol = true;
                break;
            }
        }
        if (m_chosen_name.find(unusable_suffix) != std::string::npos)
            is_unusable_suffix = true;

        if (is_unusable_symbol) {
            show_error(this,_(L("The supplied name is not valid;")) + "\n" +
                            _(L("the following characters are not allowed:")) + " " + unusable_symbols);
        }
        else if (is_unusable_suffix) {
            show_error(this,_(L("The supplied name is not valid;")) + "\n" +
                            _(L("the following suffix is not allowed:")) + "\n\t" +
                            wxString::FromUTF8(unusable_suffix.c_str()));
        }
        else if (m_chosen_name == "- default -") {
            show_error(this, _(L("The supplied name is not available.")));
        }
        else {
            EndModal(rename? wxID_APPLY :wxID_OK);
        }
    }
}

void TabSLAMaterial::build()
{
    m_presets = &m_preset_bundle->sla_materials;
    load_initial_data();

    if (create_pages("sla_material.ui")) return;

    auto page = add_options_page(_(L("Material")), "resin");

    auto optgroup = page->new_optgroup(_(L("Material")));
    optgroup->append_single_option_line("bottle_cost");
    optgroup->append_single_option_line("bottle_volume");
    optgroup->append_single_option_line("bottle_weight");
    optgroup->append_single_option_line("material_density");

    optgroup->m_on_change = [this, optgroup](t_config_option_key opt_key, boost::any value)
    {
        DynamicPrintConfig new_conf = *m_config;

        if (opt_key == "bottle_volume") {
            double new_bottle_weight =  boost::any_cast<double>(value)*(new_conf.option("material_density")->getFloat() / 1000);
            new_conf.set_key_value("bottle_weight", new ConfigOptionFloat(new_bottle_weight));
        }
        if (opt_key == "bottle_weight") {
            double new_bottle_volume =  boost::any_cast<double>(value)/new_conf.option("material_density")->getFloat() * 1000;
            new_conf.set_key_value("bottle_volume", new ConfigOptionFloat(new_bottle_volume));
        }
        if (opt_key == "material_density") {
            double new_bottle_volume = new_conf.option("bottle_weight")->getFloat() / boost::any_cast<double>(value) * 1000;
            new_conf.set_key_value("bottle_volume", new ConfigOptionFloat(new_bottle_volume));
        }

        load_config(new_conf);

        update_dirty();

        // Change of any from those options influences for an update of "Sliced Info"
        wxGetApp().sidebar().update_sliced_info_sizer();
        wxGetApp().sidebar().Layout();
    };

    optgroup = page->new_optgroup(_(L("Layers")));
    optgroup->append_single_option_line("initial_layer_height");

    optgroup = page->new_optgroup(_(L("Exposure")));
    optgroup->append_single_option_line("exposure_time");
    optgroup->append_single_option_line("initial_exposure_time");

    optgroup = page->new_optgroup(_(L("Corrections")));
    std::vector<std::string> corrections = {"material_correction"};
//    std::vector<std::string> axes{ "X", "Y", "Z" };
    std::vector<std::string> axes{ "XY", "Z" };
    for (auto& opt_key : corrections) {
        auto line = Line{ _(m_config->def()->get(opt_key)->full_label), "" };
        int id = 0;
        for (auto& axis : axes) {
            auto opt = optgroup->get_option(opt_key, id);
            opt.opt.label = axis;
            line.append_option(opt);
            ++id;
        }
        optgroup->append_line(line);
    }

    page = add_options_page(_(L("Notes")), "note");
    optgroup = page->new_optgroup(_(L("Notes")), 0);
    optgroup->title_width = 0;
    Option option = optgroup->get_option("material_notes");
    option.opt.full_width = true;
    option.opt.height = 25;//250;
    optgroup->append_single_option_line(option);

    page = add_options_page(_(L("Dependencies")), "wrench");
    optgroup = page->new_optgroup(_(L("Profile dependencies")));

    create_line_with_widget(optgroup.get(), "compatible_printers", [this](wxWindow* parent) {
        return compatible_widget_create(parent, m_compatible_printers);
    });
    
    option = optgroup->get_option("compatible_printers_condition");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    create_line_with_widget(optgroup.get(), "compatible_prints", [this](wxWindow* parent) {
        return compatible_widget_create(parent, m_compatible_prints);
    });

    option = optgroup->get_option("compatible_prints_condition");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    build_preset_description_line(optgroup.get());
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabSLAMaterial::reload_config()
{
    this->compatible_widget_reload(m_compatible_printers);
    this->compatible_widget_reload(m_compatible_prints);
    Tab::reload_config();
}

void TabSLAMaterial::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF)
        return;

// #ys_FIXME. Just a template for this function
//     m_update_cnt++;
//     ! something to update
//     m_update_cnt--;
//
//     if (m_update_cnt == 0)
        wxGetApp().mainframe->on_config_changed(m_config);
}

void TabSLAPrint::build()
{
    m_presets = &m_preset_bundle->sla_prints;
    load_initial_data();

    if (create_pages("sla_print.ui")) return;

    auto page = add_options_page(_(L("Layers and perimeters")), "layers");

    auto optgroup = page->new_optgroup(_(L("Layers")));
    optgroup->append_single_option_line("layer_height");
    optgroup->append_single_option_line("faded_layers");

    page = add_options_page(_(L("Supports")), "support"/*"sla_supports"*/);
    optgroup = page->new_optgroup(_(L("Supports")));
    optgroup->append_single_option_line("supports_enable");

    optgroup = page->new_optgroup(_(L("Support head")));
    optgroup->append_single_option_line("support_head_front_diameter");
    optgroup->append_single_option_line("support_head_penetration");
    optgroup->append_single_option_line("support_head_width");

    optgroup = page->new_optgroup(_(L("Support pillar")));
    optgroup->append_single_option_line("support_pillar_diameter");
    optgroup->append_single_option_line("support_max_bridges_on_pillar");
    
    optgroup->append_single_option_line("support_pillar_connection_mode");
    optgroup->append_single_option_line("support_buildplate_only");
    // TODO: This parameter is not used at the moment.
    // optgroup->append_single_option_line("support_pillar_widening_factor");
    optgroup->append_single_option_line("support_base_diameter");
    optgroup->append_single_option_line("support_base_height");
    optgroup->append_single_option_line("support_base_safety_distance");
    
    // Mirrored parameter from Pad page for toggling elevation on the same page
    optgroup->append_single_option_line("pad_around_object");
    optgroup->append_single_option_line("support_object_elevation");

    optgroup = page->new_optgroup(_(L("Connection of the support sticks and junctions")));
    optgroup->append_single_option_line("support_critical_angle");
    optgroup->append_single_option_line("support_max_bridge_length");
    optgroup->append_single_option_line("support_max_pillar_link_distance");

    optgroup = page->new_optgroup(_(L("Automatic generation")));
    optgroup->append_single_option_line("support_points_density_relative");
    optgroup->append_single_option_line("support_points_minimal_distance");

    page = add_options_page(_(L("Pad")), "pad");
    optgroup = page->new_optgroup(_(L("Pad")));
    optgroup->append_single_option_line("pad_enable");
    optgroup->append_single_option_line("pad_wall_thickness");
    optgroup->append_single_option_line("pad_wall_height");
    optgroup->append_single_option_line("pad_brim_size");
    optgroup->append_single_option_line("pad_max_merge_distance");
    // TODO: Disabling this parameter for the beta release
//    optgroup->append_single_option_line("pad_edge_radius");
    optgroup->append_single_option_line("pad_wall_slope");

    optgroup->append_single_option_line("pad_around_object");
    optgroup->append_single_option_line("pad_around_object_everywhere");
    optgroup->append_single_option_line("pad_object_gap");
    optgroup->append_single_option_line("pad_object_connector_stride");
    optgroup->append_single_option_line("pad_object_connector_width");
    optgroup->append_single_option_line("pad_object_connector_penetration");
    
    page = add_options_page(_(L("Hollowing")), "hollowing");
    optgroup = page->new_optgroup(_(L("Hollowing")));
    optgroup->append_single_option_line("hollowing_enable");
    optgroup->append_single_option_line("hollowing_min_thickness");
    optgroup->append_single_option_line("hollowing_quality");
    optgroup->append_single_option_line("hollowing_closing_distance");

    page = add_options_page(_(L("Advanced")), "wrench");
    optgroup = page->new_optgroup(_(L("Slicing")));
    optgroup->append_single_option_line("slice_closing_radius");

    page = add_options_page(_(L("Output options")), "output+page_white");
    optgroup = page->new_optgroup(_(L("Output file")));
    Option option = optgroup->get_option("output_filename_format");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    page = add_options_page(_(L("Dependencies")), "wrench");
    optgroup = page->new_optgroup(_(L("Profile dependencies")));

    create_line_with_widget(optgroup.get(), "compatible_printers", [this](wxWindow* parent) {
        return compatible_widget_create(parent, m_compatible_printers);
    });

    option = optgroup->get_option("compatible_printers_condition");
    option.opt.full_width = true;
    optgroup->append_single_option_line(option);

    build_preset_description_line(optgroup.get());
}

// Reload current config (aka presets->edited_preset->config) into the UI fields.
void TabSLAPrint::reload_config()
{
    this->compatible_widget_reload(m_compatible_printers);
    Tab::reload_config();
}

void TabSLAPrint::update()
{
    if (m_preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF)
        return;

    m_update_cnt++;

    m_config_manipulation.update_print_sla_config(m_config, true);
    m_update_cnt--;

    if (m_update_cnt == 0) {
        m_config_manipulation.toggle_print_sla_options(m_config);

        // update() could be called during undo/redo execution
        // Update of objectList can cause a crash in this case (because m_objects doesn't match ObjectList) 
        if (!wxGetApp().plater()->inside_snapshot_capture())
            wxGetApp().obj_list()->update_and_show_object_settings_item();

        wxGetApp().mainframe->on_config_changed(m_config);
    }
}

ConfigManipulation Tab::get_config_manipulation()
{
    auto load_config = [this]()
    {
        update_dirty();
        // Initialize UI components with the config values.
        reload_config();
        update();
    };

    auto get_field_ = [this](const t_config_option_key& opt_key, int opt_index) {
        return get_field(opt_key, opt_index);
    };

    auto cb_value_change = [this](const std::string& opt_key, const boost::any& value) {
        return on_value_change(opt_key, value);
    };

    return ConfigManipulation(load_config, get_field_, cb_value_change);
}


} // GUI
} // Slic3r
