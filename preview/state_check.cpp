#include "library_state.h"
#include <cassert>
int main() {
    LibraryState state;
    state.move(-1,0,12); assert(state.selected==0);
    for(int i=0;i<4;++i) state.move(1,0,12);
    assert(state.selected==4 && state.page()==1 && state.slot()==0);
    state.move(-1,0,12);
    assert(state.selected==3 && state.page()==0 && state.slot()==3);
    state.move(0,1,12); assert(state.selected==7 && state.page()==1);
    state.move(0,-1,12); assert(state.selected==3);
    state.selected=9; state.move(1,0,10); assert(state.selected==9);
    state.move(0,1,10); assert(state.selected==9);
    state.details=true; state.move(-1,0,10); assert(state.selected==9);
    state.details=false; state.move(0,0,1); assert(state.selected==0);
    state.move(1,0,0); assert(state.selected==0);
}
