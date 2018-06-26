#include "GUI.hpp"
#include "OptionsGroup.hpp"
#include "PresetBundle.hpp"
#include "GUI_ObjectParts.hpp"
#include "Model.hpp"
#include "wxExtensions.hpp"
#include "LambdaObjectDialog.hpp"
#include "../../libslic3r/Utils.hpp"

#include <wx/msgdlg.h>
#include <wx/frame.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace Slic3r
{
namespace GUI
{
wxSizer		*m_sizer_object_buttons = nullptr;
wxSizer		*m_sizer_part_buttons = nullptr;
wxSizer		*m_sizer_object_movers = nullptr;
wxDataViewCtrl				*m_objects_ctrl = nullptr;
PrusaObjectDataViewModel	*m_objects_model = nullptr;
wxCollapsiblePane			*m_collpane_settings = nullptr;

wxIcon		m_icon_modifiermesh;
wxIcon		m_icon_solidmesh;

wxSlider*	m_mover_x = nullptr;
wxSlider*	m_mover_y = nullptr;
wxSlider*	m_mover_z = nullptr;
wxButton*	m_btn_move_up = nullptr;
wxButton*	m_btn_move_down = nullptr;
Point3		m_move_options;
Point3		m_last_coords;
int			m_selected_object_id = -1;

bool		g_prevent_list_events = false;		// We use this flag to avoid circular event handling Select() 
												// happens to fire a wxEVT_LIST_ITEM_SELECTED on OSX, whose event handler 
												// calls this method again and again and again
ModelObjectPtrs				m_objects;

int			m_event_object_selection_changed = 0;
int			m_event_object_settings_changed = 0;

bool m_parts_changed = false;
bool m_part_settings_changed = false;

void set_event_object_selection_changed(const int& event){
	m_event_object_selection_changed = event;
}
void set_event_object_settings_changed(const int& event){
	m_event_object_settings_changed = event;
}

void init_mesh_icons(){
	m_icon_modifiermesh = wxIcon(Slic3r::GUI::from_u8(Slic3r::var("plugin.png")), wxBITMAP_TYPE_PNG);
	m_icon_solidmesh = wxIcon(Slic3r::GUI::from_u8(Slic3r::var("package.png")), wxBITMAP_TYPE_PNG);
}

bool is_parts_changed(){return m_parts_changed;}
bool is_part_settings_changed(){ return m_part_settings_changed; }

static wxString dots("�", wxConvUTF8);

// ****** from GUI.cpp
wxBoxSizer* content_objects_list(wxWindow *win)
{
	m_objects_ctrl = new wxDataViewCtrl(win, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	m_objects_ctrl->SetInitialSize(wxSize(-1, 150)); // TODO - Set correct height according to the opened/closed objects

	auto objects_sz = new wxBoxSizer(wxVERTICAL);
	objects_sz->Add(m_objects_ctrl, 1, wxGROW | wxLEFT, 20);

	m_objects_model = new PrusaObjectDataViewModel;
	m_objects_ctrl->AssociateModel(m_objects_model);
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
	m_objects_ctrl->EnableDragSource(wxDF_UNICODETEXT);
	m_objects_ctrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

	// column 0(Icon+Text) of the view control:
	m_objects_ctrl->AppendIconTextColumn(_(L("Name")), 0, wxDATAVIEW_CELL_INERT, 150,
		wxALIGN_LEFT, /*wxDATAVIEW_COL_SORTABLE | */wxDATAVIEW_COL_RESIZABLE);

	// column 1 of the view control:
	m_objects_ctrl->AppendTextColumn(_(L("Copy")), 1, wxDATAVIEW_CELL_INERT, 65,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

	// column 2 of the view control:
	m_objects_ctrl->AppendTextColumn(_(L("Scale")), 2, wxDATAVIEW_CELL_INERT, 70,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

	m_objects_ctrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [](wxEvent& event)
	{
		object_ctrl_selection_changed();
	});

	m_objects_ctrl->Bind(wxEVT_KEY_DOWN, [](wxKeyEvent& event)
	{
		if (event.GetKeyCode() == WXK_TAB)
			m_objects_ctrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
		else
			event.Skip();
	});

	return objects_sz;
}

wxBoxSizer* content_edit_object_buttons(wxWindow* win)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);

	auto btn_load_part = new wxButton(win, wxID_ANY, /*Load */"part" + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_load_modifier = new wxButton(win, wxID_ANY, /*Load */"modifier" + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_load_lambda_modifier = new wxButton(win, wxID_ANY, /*Load */"generic" + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_delete = new wxButton(win, wxID_ANY, "Delete"/*" part"*/, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_split = new wxButton(win, wxID_ANY, "Split"/*" part"*/, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	m_btn_move_up = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize/*wxSize(30, -1)*/, wxBU_LEFT);
	m_btn_move_down = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize/*wxSize(30, -1)*/, wxBU_LEFT);

	//*** button's functions
	btn_load_part->Bind(wxEVT_BUTTON, [win](wxEvent&) {
		on_btn_load(win);
	});

	btn_load_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&) {
		on_btn_load(win, true);
	});

	btn_load_lambda_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&) {
		on_btn_load(win, true, true);
	});

	btn_delete		->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_del(); });
	btn_split		->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_split(); });
	m_btn_move_up	->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_move_up(); });
	m_btn_move_down	->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_move_down(); });
	//***

	m_btn_move_up->SetMinSize(wxSize(20, -1));
	m_btn_move_down->SetMinSize(wxSize(20, -1));
	btn_load_part->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_load_modifier->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_load_lambda_modifier->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_delete->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_delete.png")), wxBITMAP_TYPE_PNG));
	btn_split->SetBitmap(wxBitmap(from_u8(Slic3r::var("shape_ungroup.png")), wxBITMAP_TYPE_PNG));
	m_btn_move_up->SetBitmap(wxBitmap(from_u8(Slic3r::var("bullet_arrow_up.png")), wxBITMAP_TYPE_PNG));
	m_btn_move_down->SetBitmap(wxBitmap(from_u8(Slic3r::var("bullet_arrow_down.png")), wxBITMAP_TYPE_PNG));

	m_sizer_object_buttons = new wxGridSizer(1, 3, 0, 0);
	m_sizer_object_buttons->Add(btn_load_part, 0, wxEXPAND);
	m_sizer_object_buttons->Add(btn_load_modifier, 0, wxEXPAND);
	m_sizer_object_buttons->Add(btn_load_lambda_modifier, 0, wxEXPAND);
	m_sizer_object_buttons->Show(false);

	m_sizer_part_buttons = new wxGridSizer(1, 3, 0, 0);
	m_sizer_part_buttons->Add(btn_delete, 0, wxEXPAND);
	m_sizer_part_buttons->Add(btn_split, 0, wxEXPAND);
	{
		auto up_down_sizer = new wxGridSizer(1, 2, 0, 0);
		up_down_sizer->Add(m_btn_move_up, 1, wxEXPAND);
		up_down_sizer->Add(m_btn_move_down, 1, wxEXPAND);
		m_sizer_part_buttons->Add(up_down_sizer, 0, wxEXPAND);
	}
	m_sizer_part_buttons->Show(false);

	btn_load_part->SetFont(Slic3r::GUI::small_font());
	btn_load_modifier->SetFont(Slic3r::GUI::small_font());
	btn_load_lambda_modifier->SetFont(Slic3r::GUI::small_font());
	btn_delete->SetFont(Slic3r::GUI::small_font());
	btn_split->SetFont(Slic3r::GUI::small_font());
	m_btn_move_up->SetFont(Slic3r::GUI::small_font());
	m_btn_move_down->SetFont(Slic3r::GUI::small_font());

	sizer->Add(m_sizer_object_buttons, 0, wxEXPAND | wxLEFT, 20);
	sizer->Add(m_sizer_part_buttons, 0, wxEXPAND | wxLEFT, 20);
	return sizer;
}

void update_after_moving()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item || m_selected_object_id<0)
		return;

	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;

	Point3 m = m_move_options;
	Point3 l = m_last_coords;

	auto d = Pointf3(m.x - l.x, m.y - l.y, m.z - l.z);
	auto volume = m_objects[m_selected_object_id]->volumes[volume_id];
	volume->mesh.translate(d.x,d.y,d.z);
	m_last_coords = m;

	m_parts_changed = true;
	parts_changed(m_selected_object_id);
}

wxSizer* object_movers(wxWindow *win)
{
// 	DynamicPrintConfig* config = &get_preset_bundle()->/*full_config();//*/printers.get_edited_preset().config; // TODO get config from Model_volume
	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(win, "Move"/*, config*/);
	optgroup->label_width = 20;
	optgroup->m_on_change = [](t_config_option_key opt_key, boost::any value){
		int val = boost::any_cast<int>(value);
		bool update = false;
		if (opt_key == "x" && m_move_options.x != val){
			update = true;
			m_move_options.x = val;
		}
		else if (opt_key == "y" && m_move_options.y != val){
			update = true;
			m_move_options.y = val;
		}
		else if (opt_key == "z" && m_move_options.z != val){
			update = true;
			m_move_options.z = val;
		}
		if (update) update_after_moving();
	};

	ConfigOptionDef def;
	def.label = L("X");
	def.type = coInt;
	def.gui_type = "slider";
	def.default_value = new ConfigOptionInt(0);

	Option option = Option(def, "x");
	option.opt.full_width = true;
	optgroup->append_single_option_line(option);
	m_mover_x = dynamic_cast<wxSlider*>(optgroup->get_field("x")->getWindow());

	def.label = L("Y");
	option = Option(def, "y");
	optgroup->append_single_option_line(option);
	m_mover_y = dynamic_cast<wxSlider*>(optgroup->get_field("y")->getWindow());

	def.label = L("Z");
	option = Option(def, "z");
	optgroup->append_single_option_line(option);
	m_mover_z = dynamic_cast<wxSlider*>(optgroup->get_field("z")->getWindow());

	get_optgroups().push_back(optgroup);  // ogObjectMovers

	m_sizer_object_movers = optgroup->sizer;
	m_sizer_object_movers->Show(false);

	m_move_options = Point3(0, 0, 0);
	m_last_coords = Point3(0, 0, 0);

	return optgroup->sizer;
}

wxBoxSizer* content_settings(wxWindow *win)
{
	DynamicPrintConfig* config = &get_preset_bundle()->/*full_config();//*/printers.get_edited_preset().config; // TODO get config from Model_volume
	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(win, "Extruders", config);
	optgroup->label_width = label_width();

	Option option = optgroup->get_option("extruder");
	option.opt.default_value = new ConfigOptionInt(1);
	optgroup->append_single_option_line(option);

	get_optgroups().push_back(optgroup);  // ogObjectSettings

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(content_edit_object_buttons(win), 0, wxEXPAND, 0); // *** Edit Object Buttons***

	sizer->Add(optgroup->sizer, 1, wxEXPAND | wxLEFT, 20);

	auto add_btn = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER);
	if (wxMSW) add_btn->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	add_btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("add.png")), wxBITMAP_TYPE_PNG));
	sizer->Add(add_btn, 0, wxALIGN_LEFT | wxLEFT, 20);

	sizer->Add(object_movers(win), 0, wxEXPAND | wxLEFT, 20);

	return sizer;
}


// add Collapsible Pane to sizer
wxCollapsiblePane* add_collapsible_pane(wxWindow* parent, wxBoxSizer* sizer_parent, const wxString& name, std::function<wxSizer *(wxWindow *)> content_function)
{
#ifdef __WXMSW__
	auto *collpane = new PrusaCollapsiblePaneMSW(parent, wxID_ANY, name);
#else
	auto *collpane = new PrusaCollapsiblePane/*wxCollapsiblePane*/(parent, wxID_ANY, name);
#endif // __WXMSW__
	// add the pane with a zero proportion value to the sizer which contains it
	sizer_parent->Add(collpane, 0, wxGROW | wxALL, 0);

	wxWindow *win = collpane->GetPane();

	wxSizer *sizer = content_function(win);

	wxSizer *sizer_pane = new wxBoxSizer(wxVERTICAL);
	sizer_pane->Add(sizer, 1, wxGROW | wxEXPAND | wxBOTTOM, 2);
	win->SetSizer(sizer_pane);
	// 	sizer_pane->SetSizeHints(win);
	return collpane;
}

void add_collapsible_panes(wxWindow* parent, wxBoxSizer* sizer)
{
	// *** Objects List ***	
	auto collpane = add_collapsible_pane(parent, sizer, "Objects List:", content_objects_list);
	collpane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, ([collpane](wxCommandEvent& e){
		// 		wxWindowUpdateLocker noUpdates(g_right_panel);
		if (collpane->IsCollapsed()) {
			m_sizer_object_buttons->Show(false);
			m_sizer_part_buttons->Show(false);
			m_sizer_object_movers->Show(false);
			if (!m_objects_ctrl->HasSelection())
				m_collpane_settings->Show(false);
		}
	}));

	// *** Object/Part Settings ***
	m_collpane_settings = add_collapsible_pane(parent, sizer, "Object Settings", content_settings);
}

void show_collpane_settings(bool expert_mode)
{
	m_collpane_settings->Show(expert_mode && !m_objects_model->IsEmpty());
}

void add_object_to_list(const std::string &name, ModelObject* model_object)
{
	wxString item = name;
	int scale = model_object->instances[0]->scaling_factor * 100;
	m_objects_ctrl->Select(m_objects_model->Add(item, model_object->instances.size(), scale));
// 	part_selection_changed();
#ifdef __WXMSW__
	object_ctrl_selection_changed();
#endif //__WXMSW__
	m_objects.push_back(model_object);
}

void delete_object_from_list()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item || m_objects_model->GetParent(item) != wxDataViewItem(0))
		return;
// 	m_objects_ctrl->Select(m_objects_model->Delete(item));
	m_objects_model->Delete(item);

	if (m_objects_model->IsEmpty())
		m_collpane_settings->Show(false);
}

void delete_all_objects_from_list()
{
	m_objects_model->DeleteAll();
	m_collpane_settings->Show(false);
}

void set_object_count(int idx, int count)
{
	m_objects_model->SetValue(wxString::Format("%d", count), idx, 1);
	m_objects_ctrl->Refresh();
}

void set_object_scale(int idx, int scale)
{
	m_objects_model->SetValue(wxString::Format("%d%%", scale), idx, 2);
	m_objects_ctrl->Refresh();
}

void unselect_objects()
{
	m_objects_ctrl->UnselectAll();
	part_selection_changed();
}

void select_current_object(int idx)
{
	g_prevent_list_events = true;
	m_objects_ctrl->UnselectAll();
	if (idx < 0) {
		g_prevent_list_events = false;
		return;
	}
	m_objects_ctrl->Select(m_objects_model->GetItemById(idx));
	part_selection_changed();
	g_prevent_list_events = false;
}

void object_ctrl_selection_changed()
{
	if (g_prevent_list_events) return;

	part_selection_changed();

// 	if (m_selected_object_id < 0) return;

	if (m_event_object_selection_changed > 0) {
		wxCommandEvent event(m_event_object_selection_changed);
		event.SetInt(int(m_objects_model->GetParent(/*item*/ m_objects_ctrl->GetSelection()) != wxDataViewItem(0)));
		event.SetId(m_selected_object_id);
		get_main_frame()->ProcessWindowEvent(event);
	}
}

// ******

void load_part(	wxWindow* parent, ModelObject* model_object, 
				wxArrayString& part_names, const bool is_modifier)
{
	wxArrayString input_files;
	open_model(parent, input_files);
	for (int i = 0; i < input_files.size(); ++i) {
		std::string input_file = input_files.Item(i).ToStdString();

		Model model;
		try {
			model = Model::read_from_file(input_file);
		}
		catch (std::exception &e) {
			auto msg = _(L("Error! ")) + input_file + " : " + e.what() + ".";
			show_error(parent, msg);
			exit(1);
		}

		for ( auto object : model.objects) {
			for (auto volume : object->volumes) {
				auto new_volume = model_object->add_volume(*volume);
				new_volume->modifier = is_modifier;
				boost::filesystem::path(input_file).filename().string();
				new_volume->name = boost::filesystem::path(input_file).filename().string();

				part_names.Add(new_volume->name);

				// apply the same translation we applied to the object
				new_volume->mesh.translate( model_object->origin_translation.x,
											model_object->origin_translation.y, 
											model_object->origin_translation.y );
				// set a default extruder value, since user can't add it manually
				new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

				m_parts_changed = true;
			}
		}
	}
}

void load_lambda(	wxWindow* parent, ModelObject* model_object,
					wxArrayString& part_names, const bool is_modifier)
{
	auto dlg = new LambdaObjectDialog(parent);
	if (dlg->ShowModal() == wxID_CANCEL) {
		return;
	}

	std::string name = "lambda-";
	TriangleMesh mesh;

	auto params = dlg->ObjectParameters();
	switch (params.type)
	{
	case LambdaTypeBox:{
		mesh = make_cube(params.dim[0], params.dim[1], params.dim[2]);
		name += "Box";
		break;}
	case LambdaTypeCylinder:{
		mesh = make_cylinder(params.cyl_r, params.cyl_h);
		name += "Cylinder";
		break;}
	case LambdaTypeSphere:{
		mesh = make_sphere(params.sph_rho);
		name += "Sphere";
		break;}
	case LambdaTypeSlab:{
		const auto& size = model_object->bounding_box().size();
		mesh = make_cube(size.x*1.5, size.y*1.5, params.slab_h);
		// box sets the base coordinate at 0, 0, move to center of plate and move it up to initial_z
		mesh.translate(-size.x*1.5 / 2.0, -size.y*1.5 / 2.0, params.slab_z);
		name += "Slab";
		break; }
	default:
		break;
	}
	mesh.repair();

	auto new_volume = model_object->add_volume(mesh);
	new_volume->modifier = is_modifier;
	new_volume->name = name;
	// set a default extruder value, since user can't add it manually
	new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

	part_names.Add(name);

	m_parts_changed = true;
}

void on_btn_load(wxWindow* parent, bool is_modifier /*= false*/, bool is_lambda/* = false*/)
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	int obj_idx = -1;
	if (m_objects_model->GetParent(item) == wxDataViewItem(0))
		obj_idx = m_objects_model->GetIdByItem(item);
	else
		return;

	if (obj_idx < 0) return;
	wxArrayString part_names;
	if (is_lambda)
		load_lambda(parent, m_objects[obj_idx], part_names, is_modifier);
	else
		load_part(parent, m_objects[obj_idx], part_names, is_modifier);

	parts_changed(obj_idx);

	for (int i = 0; i < part_names.size(); ++i)
		m_objects_ctrl->Select(	m_objects_model->AddChild(item, part_names.Item(i), 
								is_modifier ? m_icon_modifiermesh : m_icon_solidmesh));
// 	part_selection_changed();
#ifdef __WXMSW__
	object_ctrl_selection_changed();
#endif //__WXMSW__
}

void on_btn_del()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item) return;

	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;
	auto volume = m_objects[m_selected_object_id]->volumes[volume_id];

	// if user is deleting the last solid part, throw error
	int solid_cnt = 0;
	for (auto vol : m_objects[m_selected_object_id]->volumes)
		if (!vol->modifier)
			++solid_cnt;
	if (!volume->modifier && solid_cnt == 1) {
		Slic3r::GUI::show_error(nullptr, _(L("You can't delete the last solid part from this object.")));
		return;
	}

	m_objects[m_selected_object_id]->delete_volume(volume_id);
	m_parts_changed = true;

	parts_changed(m_selected_object_id);

	m_objects_ctrl->Select(m_objects_model->Delete(item));
	part_selection_changed();
// #ifdef __WXMSW__
// 	object_ctrl_selection_changed();
// #endif //__WXMSW__
}

void on_btn_split()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;

	auto volume = m_objects[m_selected_object_id]->volumes[volume_id];
	DynamicPrintConfig&	config = get_preset_bundle()->prints.get_edited_preset().config;
	auto nozzle_dmrs_cnt = config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
	if (volume->split(nozzle_dmrs_cnt) > 1)	{
		// TODO update model
		m_parts_changed = true;
		parts_changed(m_selected_object_id);
	}
}

void on_btn_move_up(){
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;
	auto& volumes = m_objects[m_selected_object_id]->volumes;
	if (0 < volume_id && volume_id < volumes.size()) {
		std::swap(volumes[volume_id - 1], volumes[volume_id]);
		m_parts_changed = true;
		m_objects_ctrl->Select(m_objects_model->MoveChildUp(item));
		part_selection_changed();
// #ifdef __WXMSW__
// 		object_ctrl_selection_changed();
// #endif //__WXMSW__
	}
}

void on_btn_move_down(){
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;
	auto& volumes = m_objects[m_selected_object_id]->volumes;
	if (0 <= volume_id && volume_id+1 < volumes.size()) {
		std::swap(volumes[volume_id + 1], volumes[volume_id]);
		m_parts_changed = true;
		m_objects_ctrl->Select(m_objects_model->MoveChildDown(item));
		part_selection_changed();
// #ifdef __WXMSW__
// 		object_ctrl_selection_changed();
// #endif //__WXMSW__
	}
}

void parts_changed(int obj_idx)
{ 
	if (m_event_object_settings_changed <= 0) return;

	wxCommandEvent e(m_event_object_settings_changed);
	auto event_str = wxString::Format("%d %d %d", obj_idx,
		is_parts_changed() ? 1 : 0,
		is_part_settings_changed() ? 1 : 0);
	e.SetString(event_str);
	get_main_frame()->ProcessWindowEvent(e);
}

void part_selection_changed()
{
	auto item = m_objects_ctrl->GetSelection();
	int obj_idx = -1;
	if (item)
	{
		if (m_objects_model->GetParent(item) == wxDataViewItem(0))
			obj_idx = m_objects_model->GetIdByItem(item);
		else {
			auto parent = m_objects_model->GetParent(item);
			// Take ID of the parent object to "inform" perl-side which object have to be selected on the scene
			obj_idx = m_objects_model->GetIdByItem(parent);
		}
	}
	m_selected_object_id = obj_idx;

	wxWindowUpdateLocker noUpdates(get_right_panel());

	m_move_options = Point3(0, 0, 0);
	m_last_coords = Point3(0, 0, 0);
	// reset move sliders
	std::vector<std::string> opt_keys = {"x", "y", "z"};
	auto og = get_optgroup(ogObjectMovers);
	for (auto opt_key: opt_keys)
		og->set_value(opt_key, int(0));

	if (/*!item || */m_selected_object_id < 0){
		m_sizer_object_buttons->Show(false);
		m_sizer_part_buttons->Show(false);
		m_sizer_object_movers->Show(false);
		m_collpane_settings->Show(false);
		return;
	}

	m_collpane_settings->Show(true);

	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0){
		m_sizer_object_buttons->Show(true);
		m_sizer_part_buttons->Show(false);
		m_sizer_object_movers->Show(false);
		m_collpane_settings->SetLabelText(_(L("Object Settings")) + ":");

// 		elsif($itemData->{type} eq 'object') {
// 			# select nothing in 3D preview
// 
// 			# attach object config to settings panel
// 			$self->{optgroup_movers}->disable;
// 			$self->{staticbox}->SetLabel('Object Settings');
// 			@opt_keys = (map @{$_->get_keys}, Slic3r::Config::PrintObject->new, Slic3r::Config::PrintRegion->new);
// 			$config = $self->{model_object}->config;
// 		}

		return;
	}

	m_collpane_settings->SetLabelText(_(L("Part Settings")) + ":");
	
	m_sizer_object_buttons->Show(false);
	m_sizer_part_buttons->Show(true);
	m_sizer_object_movers->Show(true);

	auto bb_size = m_objects[m_selected_object_id]->bounding_box().size();
	int scale = 10; //??

	m_mover_x->SetMin(-bb_size.x * 4 * scale);
	m_mover_x->SetMax(bb_size.x * 4 * scale);

	m_mover_y->SetMin(-bb_size.y * 4 * scale);
	m_mover_y->SetMax(bb_size.y * 4 * scale);

	m_mover_z->SetMin(-bb_size.z * 4 * scale);
	m_mover_z->SetMax(bb_size.z * 4 * scale);


	
//	my ($config, @opt_keys);
	m_btn_move_up->Enable(volume_id > 0);
	m_btn_move_down->Enable(volume_id + 1 < m_objects[m_selected_object_id]->volumes.size());

	// attach volume config to settings panel
	auto volume = m_objects[m_selected_object_id]->volumes[volume_id];

	if (volume->modifier) 
		og->enable();
	else 
		og->disable();

//	auto config = volume->config;

	// get default values
// 	@opt_keys = @{Slic3r::Config::PrintRegion->new->get_keys};
// 	} 
/*	
	# get default values
	my $default_config = Slic3r::Config::new_from_defaults_keys(\@opt_keys);

	# append default extruder
	push @opt_keys, 'extruder';
	$default_config->set('extruder', 0);
	$config->set_ifndef('extruder', 0);
	$self->{settings_panel}->set_default_config($default_config);
	$self->{settings_panel}->set_config($config);
	$self->{settings_panel}->set_opt_keys(\@opt_keys);
	$self->{settings_panel}->set_fixed_options([qw(extruder)]);
	$self->{settings_panel}->enable;
	}
	 */
}

} //namespace GUI
} //namespace Slic3r 