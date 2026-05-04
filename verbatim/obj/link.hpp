#pragma once

#include "tools/objalloc.hpp"
#include "tools/structs.hpp"

#include "shared/defs.hpp"

#include <cstddef>
#include <cassert>
#include <utility>

namespace obj {


template<typename T>
struct Link : T {
private:

    Link *nxt = NULL;
    Link *prv = NULL;


    static Link *alloc_link() {
        return (Link *)objalloc::malloc<Link>();
    }

public:

    Link()  = delete;
    ~Link() = default;

    Link &operator=(const Link &) = delete;
    Link &operator=(Link &&)      = delete;


    // Get a link pointer from a regular pointer to T
    static Link *from_regular(T *ptr) {
        return (Link *)ptr;
    }


    static Link *create(const T &val)
    {
        Link *link = alloc_link();
        new (link) Link(val);
        return link;
    }

    static Link *create(T &&val)
    {
        Link *link = alloc_link();
        new (link) Link(std::move(val));
        return link;
    }

    static Link *create()
    {
        Link *link = alloc_link();
        new (link) Link();
        return link;
    }

    static void destroy(Link *link)
    {
        link->~Link();
        objalloc::free<Link>(link);
    }


    Link &operator=(const T &val)
    {
        T::operator=(val);
        return *this;
    }

    Link &operator=(T &&val)
    {
        T::operator=(std::move(val));
        return *this;
    }


    void insert_above(Link *link)
    {
        assert(link->next() == link->prev() == NULL);

        if (nxt)
        {
            nxt->prv = link;
            link->nxt = nxt;
        }

        link->prv = this;
        nxt = link;
    }

    void insert_below(Link *link)
    {
        assert(link->next() == NULL && link->prev() == NULL);

        if (prv)
        {
            prv->nxt = link;
            link->prv = prv;
        }

        link->nxt = this;
        prv = link;
    }


    // Pops and destroys the link
    void pop()
    {
        if (nxt) nxt->prv = prv;
        if (prv) prv->nxt = nxt;

        Link::destroy(this);
    }


    Link *next() { return nxt; }
    Link *prev() { return prv; }

};


// Pointer to a linked list, automatically frees the list on destruction
template<typename LinkType>
struct LinkPtr {

    LinkType *link = NULL;


    LinkPtr() = default;
    LinkPtr(LinkType *link) : link(link) {}

    ~LinkPtr()
    {
        assert(!link || !link->prev());

        LinkType *next = link;
        while (next)
        {
            LinkType *cur = next;
            next = cur->next();

            LinkType::destroy(cur);
        }
    }


    LinkPtr(LinkPtr &&other) {
        link = other.link;
        other.link = NULL;
    }
    LinkPtr &operator=(LinkPtr &&other) {
        return tools::assign_move_method(this, other);
    }

};


// For building a linked list.
// Does not destruct the linked list; expects you to take and manage it.
template<typename Link>
struct LinkList : NoCopyMove {
private:

    Link *_top = NULL;
    Link *_bottom = NULL;
    ObjSize _nlinks = 0;

private:

    void set_first(Link *link) {
        _top = _bottom = link;
    }

public:

    Link *top()    const { return _top;    }
    Link *bottom() const { return _bottom; }
    ObjSize size() const { return _nlinks; }

    ~LinkList()
    {
        // Destruct by taking the pointer and having that destructed.
        // This resets the builder, and creates and immediately destructs the chain as a LinkPtr.
        this->as_chain();
    }


    // Push a value to the top
    void push(Link *link)
    {
        _nlinks++;

        if (!_top) set_first(link);
        else
        {
            _top->insert_above(link);
            _top = link;
        }
    }

    Link *push()
    {
        Link *link = Link::create();
        push(link);
        return link;
    }

    // Enqueue a value at the bottom
    void enqueue(Link *link)
    {
        _nlinks++;

        if (!_bottom) set_first(link);
        else
        {
            _bottom->insert_below(link);
            _bottom = link;
        }
    }

    Link *enqueue()
    {
        Link *link = Link::create();
        enqueue(link);
        return link;
    }

    // Pop a link from the list and destroy it.
    // This pop method should be used instead of the link's pop method in linked lists.
    void pop(Link *link)
    {
        if (_top == link)
            _top = link->prev();
        if (_bottom == link)
            _bottom = link->next();

        link->pop();
    }

    
    // Returns the bottom link of the chain. This resets the list.
    LinkPtr<Link> *as_chain()
    {
        Link *link = _bottom;
        _top = _bottom = NULL;
        return LinkPtr<Link>(link);
    }

};


}

