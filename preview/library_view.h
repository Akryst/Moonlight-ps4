#pragma once
#include "catalog.h"
#include "library_state.h"
#include "settings.h"
#include "spatial.h"
#include <png.h>
#include <cmath>

using namespace c2d;
struct Game { std::string name, genre, caption; Color color; std::string cover; };
static const std::vector<Game> demo_games = {
    {"NEON DRIFT", "RACING", "The city comes alive after dark.", {44,211,221}, ""},
    {"ORBITAL", "EXPLORATION", "A journey beyond the familiar.", {151,134,244}, ""},
    {"SILENT PEAKS", "ADVENTURE", "Find your way through the peaks.", {128,193,166}, ""},
    {"EMBER", "ACTION", "Every new beginning starts with a spark.", {234,151,101}, ""},
    {"DEEP BLUE", "EXPLORATION", "A whole world beneath the surface.", {79,162,230}, ""},
    {"AFTER HOURS", "STRATEGY", "One last move before dawn.", {214,151,204}, ""}
};

class Library : public C2DRenderer {
public:
    Library(std::vector<Game> items, const std::filesystem::path &profile) :
        C2DRenderer({1280,720}), games(std::move(items)), profile_path(profile) {
        if (!available) return;
        SDL_SetWindowTitle(getWindow(), "Moonlight PS4 | Library");
        if (!getFont()->loadFromFile(std::string(PREVIEW_ASSETS)+"/DejaVuSans.ttf"))
            throw std::runtime_error("Cannot load interface font");
        getInput()->setRepeatDelay(180);
        try { settings = load_settings(profile); }
        catch (const std::exception &) { notice = "Could not read profile. Default settings are shown."; }
        setClearColor({9,13,22});
        for (int y=0;y<720;y+=16) rect(0,y,1280,16,{9+y/240,13+y/180,22+y/100});

        library_scene=group(); drawing=library_scene;
        hero_art=group();
        for(int x=0;x<1280;x+=32)
            rect(x,0,32,640,{9,13,22,static_cast<unsigned char>(250-x*207/1280)});
        for(int y=256;y<640;y+=16)
            rect(0,y,1280,16,{9,13,22,static_cast<unsigned char>((y-256)*255/368)});
        desktop_art=group(); drawing=desktop_art;
        // Original desktop illustration; a monitor silhouette avoids enlarging an app icon.
        rounded(827,161,333,207,{0,0,0,80},12);
        rounded(809,144,333,207,{69,91,124},12);
        rounded(816,151,319,190,{15,28,49},8);
        for(int y=0;y<158;y+=8) rect(824,159+y,303,8,{18+y/8,39+y/7,68+y/5});
        rect(949,351,52,29,{50,68,95});
        rounded(910,379,130,7,{92,116,149},3);
        crystal=new spatial::Crystal();crystal->setPosition(976,239);crystal->setScale(0.36f,0.36f);own(crystal);
        drawing=library_scene;
        text("YOUR LIBRARY",13,64,128,{170,188,212});
        title=text("",46,61,174,{245,248,255});
        caption=text("",18,64,245,{178,193,215});
        rounded(64,304,202,52,{228,237,251},9);
        glyph(spatial::Button::Cross,78,314);
        text("View details",17,119,321,{18,28,46});
        profile_label=text("",14,64,389,{141,164,195});
        count_label=text("",14,1040,397,{141,164,195});
        focus=rounded(58,444,276,160,{170,203,252},10);
        for(int i=0;i<4;++i) {
            drawing=library_scene;
            auto &slot=slots[i];
            slot.panel=group();slot.panel->setSize(264,196);
            slot.panel->setOrigin(Origin::Center);slot.panel->setPosition(196+i*296,548);
            drawing=slot.panel;
            rounded(0,9,264,148,{0,0,0,110},6);
            auto color=demo_games[i].color;
            rect(0,0,264,148,{color.r/5,color.g/5,color.b/5});
            circle(147,20,48,{color.r/3,color.g/3,color.b/3});
            text("moonlight",22,23,63,{color.r,color.g,color.b,210});
            slot.backdrop=rect(0,0,264,148,{16,24,39});
            slot.backdrop->setVisibility(Visibility::Hidden);
            slot.name=text("",17,1,165,{230,239,253});
        }
        drawing=nullptr;
        settings_scene=group();drawing=settings_scene;
        text("Settings",42,61,126,{242,247,255});
        text("Make yourself at home.",18,64,184,{147,170,199});
        rounded(64,235,744,403,{17,25,40},12);
        text("VIDEO",12,86,243,{136,161,196});
        settings_focus=rounded(78,260,716,64,{36,51,75},8);
        const char *names[]={"Resolution","Frame rate","Bitrate","Interface motion"};
        const char *descriptions[]={"Image size","Video smoothness","Quality and network usage","Animation and depth"};
        for(int i=0;i<4;++i) {
            const int y=row_y(i);
            text(names[i],19,100,y+8,{229,237,250});
            text(descriptions[i],13,100,y+36,{135,157,187});
            values[i]=text("",18,563,y+22,{187,214,254});
        }
        rect(88,477,696,1,{37,49,68});
        text("INTERFACE",12,86,496,{136,161,196});
        restore_button=rounded(64,579,241,48,{29,41,61},8);
        glyph(spatial::Button::Square,76,587);
        text("Restore defaults",16,115,595,{205,219,240});
        save_button=rounded(548,579,260,48,{228,237,251},8);
        glyph(spatial::Button::Cross,565,587);
        text("Save profile",17,605,595,{18,28,46});
        text("A LITTLE GUIDANCE",12,862,253,{143,168,202});
        help_title=text("",26,860,295,{237,244,255});
        help_body=text("",17,862,347,{158,181,211});
        text("Video options are saved to this\npreview profile. Streaming\nintegration is still pending.",14,862,548,{123,147,179});
        drawing=nullptr;
        settings_scene->setVisibility(Visibility::Hidden);

        // One navigation bar and generous TV margins keep controller navigation readable.
        rect(0,0,1280,96,{9,13,22,165});
        circle(64,39,15,{193,215,247});circle(73,34,14,{10,15,25});
        text("moonlight",23,104,41,{240,246,255});
        glyph(spatial::Button::L1,371,38);
        library_tab=text("Library",20,422,43,{239,245,255});
        settings_tab=text("Settings",20,574,43,{128,150,182});
        glyph(spatial::Button::R1,700,38);
        tab_line=rounded(422,83,72,3,{182,211,251},1);
        text("PS4",14,1185,47,{146,168,198});
        rect(64,647,1152,1,{39,52,73});
        glyph(spatial::Button::Dpad,64,650);text("Navigate",15,108,661,{160,181,208});
        glyph(spatial::Button::Cross,289,650);
        confirm_hint=text("View details",15,333,661,{160,181,208});
        glyph(spatial::Button::Circle,549,650);text("Back",15,593,661,{160,181,208});
        glyph(spatial::Button::Options,779,650);text("Settings",15,839,661,{160,181,208});
        notice_label=text("",12,66,631,{181,210,249});
        refresh();
    }
    bool running=true;
    LibraryState state;

    void open_settings() { settings_mode=true; state.details=false; refresh(); }
    void settle() {
        for(int i=0;i<4;++i) {
            slots[i].panel->setScale(state.slot()==i?1.02f:1, state.slot()==i?1.02f:1);
            slots[i].panel->setPosition(196+i*296,state.slot()==i?545:548);
        }
        focus->setPosition(58+state.slot()*296,444);
        refresh();
    }
protected:
    bool onInput(Input::Player *players) override {
        auto buttons=players[0].buttons;
        pad=static_cast<SDL_GameController *>(players[0].data);
        if(buttons & Input::Quit) running=false;
        if((buttons & Input::Start) || (buttons & Input::RB)) open_settings();
        if(buttons & Input::Y) open_settings();
        if(buttons & Input::LB) { settings_mode=false; refresh(); }
        if((buttons & Input::B) && !held_b) {
            if(settings_mode) settings_mode=false;
            else if(state.details) state.details=false;
            else running=false;
        }
        if(settings_mode) {
            if(buttons & Input::X) { settings=PreviewSettings{};notice="Defaults restored. Save to keep them."; }
            if(buttons & Input::Up) setting_row=std::max(0,setting_row-1);
            else if(buttons & Input::Down) setting_row=std::min(5,setting_row+1);
            if(buttons & Input::Left) adjust(-1);
            else if(buttons & Input::Right) adjust(1);
            if((buttons & Input::A) && !held_a) activate();
        } else {
            if((buttons & Input::A) && !held_a && !games.empty()) state.details=true;
            if((buttons & Input::X) && !games.empty()) state.details=true;
            if(buttons & Input::Left) state.move(-1,0,games.size());
            else if(buttons & Input::Right) state.move(1,0,games.size());
            else if(buttons & Input::Up) state.move(0,-1,games.size());
            else if(buttons & Input::Down) state.move(0,1,games.size());
        }
        refresh(); return true;
    }
    void onUpdate() override {
        C2DRenderer::onUpdate();
        auto *keys=SDL_GetKeyboardState(nullptr);
        held_a=keys[SDL_SCANCODE_RETURN] || (pad && SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A));
        held_b=keys[SDL_SCANCODE_ESCAPE] || (pad && SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_B));
        int x,y;
        bool mouse=(SDL_GetMouseState(&x,&y)&SDL_BUTTON(SDL_BUTTON_LEFT))!=0;
        if(mouse && !held_mouse) {
            if((x>=560 && x<740 && y<96) || (x>=779 && x<960 && y>=650 && y<684)) open_settings();
            else if(x>=370 && x<550 && y<96) { settings_mode=false; refresh(); }
            else if(settings_mode && x>=64 && x<808) {
                for(int i=0;i<4;++i) if(y>=row_y(i) && y<row_y(i)+64) {
                    setting_row=i; adjust(x>=550?(x<595?-1:1):0);
                }
                if(y>=579 && y<627 && (x<305 || x>=548)) { setting_row=x<305?4:5; activate(); }
                refresh();
            } else if(!settings_mode && y>=444 && y<=646 && x>=58) {
                int slot=(x-58)/296, index=state.page()*4+slot;
                if(slot<4 && x<64+slot*296+264 && index<static_cast<int>(games.size())) {
                    state.selected=index; state.details=false; refresh();
                }
            } else if(!settings_mode && x>=64 && x<=266 && y>=304 && y<=356 && !games.empty()) { state.details=true; refresh(); }
        }
        held_mouse=mouse;
        float blend=settings.reduced_motion?1:1-std::exp(-18*std::min(0.05f,getDeltaTime().asSeconds()));
        auto p=focus->getPosition(); focus->setPosition(p.x+(58+state.slot()*296-p.x)*blend,444);
        for(int i=0;i<4;++i) {
            float target=state.slot()==i?1.02f:1;
            float scale=slots[i].panel->getScale().x;
            scale+=(target-scale)*blend;
            slots[i].panel->setScale(scale,scale);
            auto position=slots[i].panel->getPosition();
            const float y=state.slot()==i?545:548;
            slots[i].panel->setPosition(position.x,position.y+(y-position.y)*blend);
        }
    }
private:
    struct Slot { Rectangle *panel=nullptr; RectangleShape *backdrop=nullptr; Text *name=nullptr; C2DTexture *cover=nullptr; };
    std::array<Slot,4> slots{};
    std::vector<Game> games;
    std::filesystem::path profile_path;
    PreviewSettings settings;
    bool settings_mode=false,held_a=false,held_b=false,held_mouse=false;
    int setting_row=0, shown_page=-1, shown_game=-1;
    SDL_GameController *pad=nullptr;
    std::string notice;
    C2DObject *drawing=nullptr;
    Rectangle *library_scene=nullptr,*settings_scene=nullptr,*hero_art=nullptr,*desktop_art=nullptr;
    ConvexShape *focus=nullptr,*settings_focus=nullptr,*save_button=nullptr,*restore_button=nullptr,*tab_line=nullptr;
    Text *title=nullptr,*caption=nullptr,*count_label=nullptr,*profile_label=nullptr,*notice_label=nullptr,*library_tab=nullptr,*settings_tab=nullptr,*help_title=nullptr,*help_body=nullptr,*confirm_hint=nullptr;
    std::array<Text *,4> values{};
    C2DTexture *hero_texture=nullptr;
    spatial::Crystal *crystal=nullptr;
    static int row_y(int i) { constexpr int positions[]={260,328,396,514};return positions[i]; }

    void own(C2DObject *item) { (drawing?drawing:this)->add(item); }
    Rectangle *group(float x=0,float y=0) { auto *g=new Rectangle(Vector2f(0,0)); g->setPosition(x,y); own(g); return g; }
    RectangleShape *rect(float x,float y,float w,float h,Color c) { auto *r=new RectangleShape(Vector2f(w,h)); r->setPosition(x,y);r->setFillColor(c);own(r);return r; }
    void circle(float x,float y,float r,Color c) { auto *p=new CircleShape(r,64);p->setPosition(x,y);p->setFillColor(c);own(p); }
    ConvexShape *rounded(float x,float y,float w,float h,Color c,float radius) {
        auto *p=new ConvexShape(32);
        for(int corner=0;corner<4;++corner) for(int j=0;j<8;++j) {
            float angle=(corner*90+j*90.0f/7)*0.0174532925f;
            float cx=(corner==0||corner==3)?w-radius:radius, cy=corner<2?h-radius:radius;
            p->setPoint(corner*8+j,{cx+std::cos(angle)*radius,cy+std::sin(angle)*radius});
        }
        p->setPosition(x,y);p->setFillColor(c);own(p);return p;
    }
    Text *text(const std::string &v,int size,float x,float y,Color c) { auto *t=new Text(v,size,getFont());t->setPosition(x,y);t->setFillColor(c);own(t);return t; }
    void glyph(spatial::Button b,float x,float y) {
        static const char *names[]={"cross","circle","square","triangle","options","dpad","l1","r1"};
        auto *image=new C2DTexture(std::string(PREVIEW_ASSETS)+"/buttons/"+names[static_cast<int>(b)]+".png");
        if(!image->available) { delete image;throw std::runtime_error("Cannot load controller image"); }
        image->setSize(b==spatial::Button::Options || b==spatial::Button::L1 || b==spatial::Button::R1?48:32,32);
        image->setPosition(x,y);own(image);
    }
    static std::string fit(const std::string &value,std::size_t limit) {
        if(value.size()<=limit) return value;
        while(limit>0 && (static_cast<unsigned char>(value[limit])&0xc0)==0x80) --limit;
        return value.substr(0,limit)+"...";
    }
    static C2DTexture *cover(const std::string &path,float w,float h) {
        if(path.empty()) return nullptr;
        std::error_code error;
        if(std::filesystem::file_size(path,error)>16*1024*1024 || error) return nullptr;
        png_image image{};image.version=PNG_IMAGE_VERSION;
        bool valid=png_image_begin_read_from_file(&image,path.c_str())!=0;
        valid=valid && image.width>0 && image.height>0 && image.width<=4096 && image.height<=4096;
        png_image_free(&image);if(!valid) return nullptr;
        auto *texture=new C2DTexture(path);
        if(!texture->available) { delete texture;return nullptr; }
        auto size=texture->getSize();float ratio=w/h;
        if(size.x<size.y) {
            const float width=size.x*h/size.y;
            texture->setSize(width,h);texture->setPosition((w-width)/2,0);return texture;
        }
        int cw=size.x,ch=size.y;
        if(size.x/size.y>ratio) cw=std::max(1,static_cast<int>(size.y*ratio)); else ch=std::max(1,static_cast<int>(size.x/ratio));
        texture->setTextureRect({(static_cast<int>(size.x)-cw)/2,(static_cast<int>(size.y)-ch)/2,cw,ch});
        texture->setSize(w,h);return texture;
    }
    void show_page() {
        shown_page=state.page();
        for(int i=0;i<4;++i) {
            auto &slot=slots[i];slot.backdrop->setVisibility(Visibility::Hidden);
            if(slot.cover) { slot.panel->remove(slot.cover);delete slot.cover;slot.cover=nullptr; }
            int index=shown_page*4+i;bool visible=index<static_cast<int>(games.size());
            slot.panel->setVisibility(visible?Visibility::Visible:Visibility::Hidden);
            if(!visible) continue;
            slot.name->setString(fit(games[index].name,23));
            slot.cover=cover(games[index].cover,264,148);
            if(slot.cover) { slot.panel->add(slot.cover);slot.backdrop->setVisibility(Visibility::Visible); }
        }
        count_label->setString(std::to_string(games.size())+(games.size()==1?" APP   /   ":" APPS   /   ")+std::to_string(games.empty()?0:shown_page+1)+" OF "+std::to_string((games.size()+3)/4));
    }
    void adjust(int direction) {
        if(setting_row<4 && direction) { settings.adjust(setting_row,direction);notice="Unsaved changes"; }
    }
    void activate() {
        if(setting_row<4) adjust(1);
        else if(setting_row==4) { settings=PreviewSettings{};notice="Defaults restored. Save to keep them."; }
        else { std::string error;notice=save_settings(settings,profile_path,error)?"Profile saved":error; }
    }
    void refresh() {
        if(shown_page!=state.page()) show_page();
        library_scene->setVisibility(settings_mode?Visibility::Hidden:Visibility::Visible);
        settings_scene->setVisibility(settings_mode?Visibility::Visible:Visibility::Hidden);
        tab_line->setPosition(settings_mode?574:422,83);
        library_tab->setFillColor(settings_mode?Color(119,137,166):Color(226,237,253));
        settings_tab->setFillColor(settings_mode?Color(226,237,253):Color(119,137,166));
        for(int i=0;i<4;++i) values[i]->setString("<  "+settings.value(i)+"  >");
        settings_focus->setVisibility(setting_row<4?Visibility::Visible:Visibility::Hidden);
        if(setting_row<4) settings_focus->setPosition(78,row_y(setting_row));
        save_button->setFillColor(setting_row==5?Color(187,210,255):Color(228,237,251));
        restore_button->setFillColor(setting_row==4?Color(60,79,113):Color(28,39,60));
        const char *help_names[]={"Resolution","Frame rate","Bitrate","Interface motion","Start fresh","Ready for next time"};
        const char *help_text[]={
            "1080p brings out finer details.\n720p uses less bandwidth.\n\nMatch your connection and\nhost performance.",
            "60 FPS makes movement fluid.\n30 FPS reduces the load on\nyour host and connection.",
            "A higher bitrate preserves\nmore detail in busy scenes.\n\nLower it if your connection\nstruggles to keep up.",
            "Reduced motion removes\nfocus transitions.\n\nThe selection stays clear\nand easy to follow.",
            "Restore the default options.\nSave the profile to keep\nyour changes.",
            "Save your chosen video and\ninterface options."
        };
        help_title->setString(help_names[setting_row]);help_body->setString(help_text[setting_row]);
        crystal->reduced_motion=settings.reduced_motion;
        confirm_hint->setString(settings_mode?"Select / change":"View details");
        notice_label->setString(notice);
        profile_label->setString(settings.value(0)+"   /   "+settings.value(1));
        focus->setVisibility(!games.empty() && !settings_mode?Visibility::Visible:Visibility::Hidden);
        if(games.empty()) { desktop_art->setVisibility(Visibility::Hidden);title->setString("Your library starts here.");caption->setString("Connect to your PC to discover your games.");return; }
        auto &game=games[state.selected];
        desktop_art->setVisibility(game.name=="Desktop"?Visibility::Visible:Visibility::Hidden);
        title->setString(fit(game.name,30));title->setCharacterSize(game.name.size()>23?34:46);
        caption->setString(state.details?"Connect Moonlight to your PC to launch this app.":game.caption);
        if(shown_game!=state.selected) {
            shown_game=state.selected;
            if(hero_texture) { hero_art->remove(hero_texture);delete hero_texture;hero_texture=nullptr; }
            hero_texture=game.name=="Desktop"?nullptr:cover(game.cover,1280,640);
            if(hero_texture) { hero_texture->setFillColor({211,224,244,210});hero_art->add(hero_texture); }
        }
    }
};
