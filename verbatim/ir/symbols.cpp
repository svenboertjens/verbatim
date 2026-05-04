#include "symbols.hpp"


namespace ir {


Section *Function::intersect(Section *sec1, Section *sec2)
{
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


void Function::order_visit(Section *sec, obj::PtrMap<Section *, int> &visited, obj::Array<Section *> &reverse_order)
{
    if (visited.get(sec))
        return;
    
    visited[sec]; // Indexing implicitly creates the entry

    obj::Array<Section *> succs = sec->succeeding_sections();

    for (ObjSize i = 0; i < succs.size(); i++)
        order_visit(succs[i], visited, reverse_order);

    ObjSize rdst = reverse_order.push(sec);
    _dominance[sec].rdst = rdst;
}

obj::Array<Section *> Function::compute_reverse_order(Section *entry)
{
    obj::Array<Section *> reverse_order;
    obj::PtrMap<Section *, int> visited;

    order_visit(entry, visited, reverse_order);

    return reverse_order;
}

void Function::build_dominance()
{
    Section *entry = _sections.bottom;

    // 1. Compute reverse postorder
    obj::Array<Section *> reverse_order = compute_reverse_order(entry);

    // 2. Initialize: entry dominates itself, everything else undefined
    _dominance[entry].dom = entry;
    // (all other idom[x] = NULL implicitly)

    // 3. Iterate to fixed point in RPO order
    bool changed;
    do
    {
        changed = false;

        // Iterate in reverse order to cancel out reversedness of the array.
        // Don't iterate over the first section.
        for (ObjSize i = reverse_order.size() - 1; i > 0; i--)
        {
            Section *sec = reverse_order[i - 1];
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

