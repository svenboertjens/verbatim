#include "symbols.hpp"


namespace ir {


Section *Function::intersect(Section *sec1, Section *sec2)
{
    // This is a public function, may be called when dominance is unbuilt
    if (_dominance.size() == 0)
        build_dominance();

    Section *left = sec1;
    Section *right = sec2;

    while (left != right)
    {
        while (_dominance[left].rdst < _dominance[right].rdst)
            left = _dominance[left].dom;

        while (_dominance[right].rdst < _dominance[left].rdst)
            right = _dominance[right].dom;
    }

    return left;
}


void Function::order_visit(Section *sec, obj::PtrMap<Section *, int> &visited)
{
    if (visited.get(sec))
        return;
    
    visited[sec]; // Indexing implicitly creates the entry

    obj::Array<Section *> succs = sec->succeeding_sections();

    for (ObjSize i = 0; i < succs.size(); i++)
        order_visit(succs[i], visited);

    ObjSize rdst = _postorder.push(sec);
    _dominance[sec].rdst = rdst;
}

void Function::build_postorder()
{
    Section *entry = _sections.bottom();

    _postorder = obj::Array<Section *>();
    obj::PtrMap<Section *, int> visited;

    order_visit(entry, visited);
}

void Function::build_dominance()
{
    build_postorder();

    Section *entry = _sections.bottom();
    _dominance[entry].dom = entry;

    bool changed;
    do
    {
        changed = false;

        // Iterate backwards to go in RPO.
        // Don't iterate over the entry section, we set it to itself already.
        for (ObjSize i = _postorder.size() - 1; i > 0; i--)
        {
            Section *sec = _postorder[i - 1];
            const obj::Array<Section *> &preds = sec->preceding_sections();

            // Pick first predecessor that's already been processed
            Section *new_dom = NULL;
            ObjSize iter = 0;
            for (; iter < preds.size(); iter++)
            {
                if (_dominance[preds[iter]].dom != NULL)
                {
                    new_dom = preds[iter];
                    iter++;
                    break;
                }
            }

            // Intersect with all other already-processed predecessors
            for (; iter < preds.size(); iter++)
            {
                Section *pre = preds[iter];
                if (_dominance[pre].dom != NULL)
                    new_dom = intersect(pre, new_dom);
            }

            if (_dominance[sec].dom != new_dom)
            {
                _dominance[sec].dom = new_dom;
                changed = true;
            }
        }
    }
    while (changed);
}


}

