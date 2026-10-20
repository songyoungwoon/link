#include "link/loader.hpp"
#include "link/runtime.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <string_view>

namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

std::size_t sourcePhotos(const std::filesystem::path& folder,std::string_view prefix={}){
  if(!std::filesystem::exists(folder))return 0;
  std::size_t count=0;
  for(const auto& entry:std::filesystem::directory_iterator(folder)){
    if(!entry.is_regular_file())continue;
    if(!prefix.empty()&&!entry.path().filename().string().starts_with(prefix))continue;
    auto extension=entry.path().extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".jpg"||extension==".jpeg"||extension==".png"||extension==".bmp")++count;
  }
  return count;
}
}

void testExhibition(){
  using namespace linkworld;
  const auto source=std::filesystem::path(u8"" LINK_SOURCE_DIR);
  const auto root=source/"worlds";
  const auto catSources=source.parent_path()/"image"/std::filesystem::path(u8"흑룡이");
  const auto ownerSources=source.parent_path()/"image"/"songyoungwoon";
  const auto derivatives=root/"heukryong"/"assets";
  // Exported source packages can omit personal originals; selected display copies
  // are sufficient to build and verify the native viewer in that case.
  const auto expectedCats=std::filesystem::is_directory(catSources)?sourcePhotos(catSources):sourcePhotos(derivatives,"cat-");
  const auto expectedOwners=std::filesystem::is_directory(ownerSources)?sourcePhotos(ownerSources):sourcePhotos(derivatives,"owner-");
  const auto expectedPhotos=expectedCats+expectedOwners;
  check(expectedCats>0,"The corridor has current source photographs");
  const auto gallery=loadWorld("link://local/heukryong",root);
  check(gallery->name=="사진 공간","Gallery uses a neutral navigation label");
  check(gallery->images.size()==expectedPhotos,"Gallery inventory matches the photographs currently selected in the source folders");
  std::size_t cats=0,portraits=0;
  for(const auto& [key,path]:gallery->images){
    if(key.starts_with("assets/cat-"))++cats;
    else if(key.starts_with("assets/owner-"))++portraits;
    else check(false,"Photo inventory contains only the selected source categories");
  }
  check(cats==expectedCats&&portraits==expectedOwners,"Category counts follow the current source photographs");
  check(sourcePhotos(derivatives,"cat-")==expectedCats&&sourcePhotos(derivatives,"owner-")==expectedOwners,"No obsolete numbered display copies remain after changing the selection");
  check(gallery->nodes.size()<=Limits::nodes,"Corridor respects native node budget");
  check(gallery->views.size()>=expectedPhotos,"Each photograph has an individual viewing position");
  std::size_t framed=0,photoCount=0,leftCats=0,rightCats=0;
  double furthestPhotoZ=0;
  std::set<std::string> photos;
  for(const auto& node:gallery->nodes){
    if(node.geometry.type==Geometry::Type::Frame&&node.kind==Kind::Element)++framed;
    if(!node.image.empty()){
      ++photoCount;photos.insert(node.image);
      check(node.geometry.type==Geometry::Type::Plane,"Photographs are actual textured planes");
      check(node.parent!=noParent,"Photograph belongs to a frame component");
      check(std::any_of(gallery->nodes.begin(),gallery->nodes.end(),[&](const auto& sibling){return sibling.parent==node.parent&&sibling.kind==Kind::Element&&sibling.geometry.type==Geometry::Type::Frame;}),"Each image has a physical sibling frame rim");
      const auto& frame=gallery->nodes[node.parent];
      check(std::abs(frame.transform.position[1]-1.72)<.001,"Photographs hang at a comfortable eye height");
      check(std::abs(std::abs(frame.transform.rotation[1])-90)<.001,"Photos face inward from the corridor side walls");
      if(node.image.starts_with("assets/cat-")){
        if(frame.transform.position[0]<0)++leftCats;else ++rightCats;
        furthestPhotoZ=std::min(furthestPhotoZ,frame.transform.position[2]);
      }else check(frame.transform.position[2]>0,"Personal portrait remains by the entrance");
    }
    check(node.text.empty(),"Corridor contains no decorative text or dedication plaques");
  }
  check(framed==expectedPhotos&&photoCount==expectedPhotos&&photos.size()==expectedPhotos,"Every selected photograph is framed exactly once");
  check(leftCats==(expectedCats+1)/2&&rightCats==expectedCats/2,"Cat photographs alternate along both corridor walls");
  for(const auto& view:gallery->views)check(view.position!=view.target,"A camera view must have a direction");
  check(gallery->walk.has_value(),"Corridor enables walking");
  check(gallery->portals.size()==1,"Corridor ends in one destination portal");
  const auto& portal=gallery->portals.front();
  check(portal.address=="link://local/hello-world","Portal navigates to a different local LINK");
  check(portal.position[2]<furthestPhotoZ,"Portal stands beyond the final photograph");
  check(gallery->walk->min[2]<portal.position[2]-portal.size[2]/2,"Walkable space continues through the portal trigger");
  check(gallery->eye[1]==gallery->walk->eyeHeight,"Entry camera starts at walking eye height");

  const auto destination=loadWorld("link://local/hello-world",root);
  check(destination->name=="Hello World!","Destination displays the requested exact text");
  check(destination->images.empty(),"Greeting and service introduction do not require imported images");
  std::string glyphSequence;
  std::size_t strokes=0,introductions=0;
  bool textGroup=false;
  for(const auto& node:destination->nodes){
    if(!node.text.empty()){
      ++introductions;
      check(node.id=="link_hologram_text"&&node.geometry.type==Geometry::Type::Plane,"Only the service introduction uses a text plane");
      check(node.text.find("LINK Browser")!=std::string::npos&&node.text.find("LINK Protocol")!=std::string::npos&&node.text.find("Element · Script · Style")!=std::string::npos,"The introduction explains the browser, protocol, and authoring standards");
    }
    if(node.id=="hello_world_text"){textGroup=node.name=="Hello World!";}
    if(node.id.starts_with("hello_glyph_"))glyphSequence+=node.name;
    if(node.id.starts_with("hello_stroke_")){
      ++strokes;
      check(node.kind==Kind::Element&&node.geometry.type==Geometry::Type::Box,"Every text stroke is a solid 3D box");
      check(node.geometry.size[2]>=.3,"Letters have actual extruded depth");
    }
  }
  check(textGroup&&glyphSequence=="HelloWorld!","Geometric glyphs spell the requested message");
  check(introductions==1,"The floating introduction is a single readable service panel");
  check(strokes>50&&destination->nodes.size()<=Limits::nodes,"3D glyphs remain within the native geometry budget");
  check(destination->views.size()>=2,"Destination includes a view exposing the glyph depth");
  check(destination->walk.has_value()&&destination->eye[1]==destination->walk->eyeHeight,"Destination begins at walking eye height");
  check(destination->walk->min[0]>-17&&destination->walk->max[0]<17&&destination->walk->min[2]>-48&&destination->walk->max[2]<38,"Exploration bounds remain on the floating arrival deck");
  check(destination->fog.has_value()&&destination->fog->start>70,"Distance atmosphere preserves the nearby monument and the causeway");
  {
    Runtime arrival(destination);arrival.start();
    const auto initial=arrival.find("aperture_orbit").transform.rotation;
    arrival.update(.05);
    check(arrival.find("aperture_orbit").transform.rotation[2]>initial[2],"The aperture registers animate through an ordinary world Script");
    const auto& hologram=arrival.find("link_hologram_text");
    const auto hologramIndex=static_cast<std::size_t>(&hologram-arrival.nodes().data());
    const auto hologramMaterial=arrival.material(hologramIndex);
    check(!hologramMaterial.textBackground&&hologramMaterial.opacity>0&&hologramMaterial.opacity<1,"The introduction uses luminous glyphs over a transparent background");
    check(arrival.find("link_hologram").transform.position[1]<arrival.find("hello_world_text").transform.position[1],"The introduction floats beneath the solid greeting");
    check(arrival.find("link_hologram_scan").transform.position[1]>-1.72,"An ordinary Script advances the hologram scan line");
    bool luminousText=false;
    for(std::size_t index=0;index<arrival.nodes().size();++index)
      if(arrival.nodes()[index].definition.id.starts_with("hello_stroke_")&&arrival.material(index).emission>0)luminousText=true;
    check(luminousText,"The solid text remains legible using the standard emissive Style property");
  }
  const auto before=counters();
  for(int i=0;i<4;++i){Runtime runtime(i%2?gallery:destination);runtime.start();runtime.update(.016);}
  check(counters().activeWorlds==before.activeWorlds&&counters().activeBindings==before.activeBindings,"Corridor and destination runtimes release ownership");
}
