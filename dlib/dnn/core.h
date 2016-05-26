// Copyright (C) 2015  Davis E. King (davis@dlib.net)
// License: Boost Software License   See LICENSE.txt for the full license.
#ifndef DLIB_DNn_CORE_H_
#define DLIB_DNn_CORE_H_

#include "core_abstract.h"
#include "tensor.h"
#include <iterator>
#include <memory>
#include <sstream>
#include <type_traits>
#include "../statistics.h"
#include "../rand.h"
#include "../algs.h"
#include <utility>
#include <tuple>
#include <cmath>
#include <vector>
#include "tensor_tools.h"
#include <type_traits>



namespace dlib
{

// ----------------------------------------------------------------------------------------

    namespace impl
    {
        template <typename T, typename int_<decltype(&T::get_learning_rate_multiplier)>::type = 0>
        double get_learning_rate_multiplier (
            const T& obj,
            special_
        ) { return obj.get_learning_rate_multiplier(); }

        template <typename T>
        double get_learning_rate_multiplier ( const T& obj, general_) { return 1; }
    }
    template <typename T>
    double get_learning_rate_multiplier(const T& obj) { return impl::get_learning_rate_multiplier(obj, special_()); }

// ----------------------------------------------------------------------------------------

    namespace impl
    {
        template <typename T, typename int_<decltype(&T::get_weight_decay_multiplier)>::type = 0>
        double get_weight_decay_multiplier (
            const T& obj,
            special_
        ) { return obj.get_weight_decay_multiplier(); }

        template <typename T>
        double get_weight_decay_multiplier ( const T& obj, general_) { return 1; }
    }
    template <typename T>
    double get_weight_decay_multiplier(const T& obj) { return impl::get_weight_decay_multiplier(obj, special_()); }

// ----------------------------------------------------------------------------------------

    namespace impl
    {
        class repeat_input_layer 
        {
            /*!
                None of the declarations in this object are really used. The only reason it
                exists is to allow the repeat object to use a special input layer in its
                internal networks which will cause add_tag_layer objects that happen to be
                right at the input to not create copies of their input tensors.  So
                introducing the repeat_input_layer object allows us to optimize the
                implementation of add_tag_layer for a special case that arises when it's
                used in the context of the repeat layer.
            !*/
        public:
            typedef int input_type;
            const static unsigned int sample_expansion_factor = 1;

            template <typename input_iterator>
            void to_tensor (
                input_iterator ,
                input_iterator ,
                resizable_tensor& 
            ) const
            {
                DLIB_CASSERT(false,"This function should never be called");
            }

            friend void serialize(const repeat_input_layer&, std::ostream&){}
            friend void deserialize(repeat_input_layer&, std::istream&){}
            friend std::ostream& operator<<(std::ostream& out, const repeat_input_layer&) { out << "FUCK"; return out; }
        };

        inline std::string tensor_to_str (
            const tensor& t,
            int& min_length 
        ) 
        {
            if (t.size() == 0)
                return "";

            std::ostringstream sout;
            sout << "output size=(num:"<<  t.num_samples() << ", ";
            sout << "k:" << t.k() << ",";
            while (sout.tellp() < 28) sout << " ";
            sout << "nr:" << t.nr() << ",";
            while (sout.tellp() < 28+8) sout << " ";
            sout << "nc:" << t.nc() << ")";
            while (sout.tellp() < min_length) sout << " ";
            min_length = sout.tellp();
            sout << "\t";
            return sout.str();
        }
    }

// ----------------------------------------------------------------------------------------

    inline double log1pexp(double x)
    {
        using std::exp;
        using namespace std; // Do this instead of using std::log1p because some compilers
                             // error out otherwise (E.g. gcc 4.9 in cygwin)
        if (x <= -37)
            return exp(x);
        else if (-37 < x && x <= 18)
            return log1p(exp(x));
        else if (18 < x && x <= 33.3)
            return x + exp(-x);
        else
            return x;
    }
    
// ----------------------------------------------------------------------------------------

    // Tell us if T is one of the special layer types (i.e. add_layer, repeat, add_tag_layer, or
    // add_skip_layer).
    template <typename T> struct is_nonloss_layer_type : std::false_type {};
    // Tell us if T is an instance of add_loss_layer.
    template <typename T> struct is_loss_layer_type : std::false_type {};
    // Tell us if T is an instance of add_layer
    template <typename T> struct is_add_layer : std::false_type {};

    namespace impl
    {
        template <size_t... n>
        struct ct_integers_list {
            template <size_t m>
            struct push_back
            {
                typedef ct_integers_list<n..., m> type;
            };
        };

        template <size_t max>
        struct ct_make_integer_range
        {
            // recursively call push_back on ct_integers_list to build a range from 1 to max
            // inclusive.
            typedef typename ct_make_integer_range<max-1>::type::template push_back<max>::type type;
        };

        template <>
        struct ct_make_integer_range<0>
        {
            typedef ct_integers_list<> type;
        };

        template <size_t... indices, typename Tuple>
        auto tuple_subset(
            const Tuple& item, 
            ct_integers_list<indices...>
        ) -> decltype(std::make_tuple(std::get<indices>(item)...))
        {
            return std::make_tuple(std::get<indices>(item)...);
        }

        template <typename Head, typename... Tail>
        std::tuple<Tail...> basic_tuple_tail(
            const std::tuple<Head, Tail...>& item
        )
        {
            return tuple_subset(item, typename ct_make_integer_range<sizeof...(Tail)>::type());
        }

        template <typename T>
        std::tuple<T> tuple_flatten(const T& t) 
        {
            return std::make_tuple(t);
        }

        template <typename... T>
        auto tuple_flatten(
            const std::tuple<T...>& item
        ) -> decltype(tuple_flatten(item, typename ct_make_integer_range<sizeof...(T)>::type()))
        {
            return tuple_flatten(item, typename ct_make_integer_range<sizeof...(T)>::type());
        }

        template <size_t... indices, typename... T>
        auto tuple_flatten(
            const std::tuple<T...>& item, 
            ct_integers_list<indices...>
        ) -> decltype(std::tuple_cat(tuple_flatten(std::get<indices-1>(item))...))
        {
            return std::tuple_cat(tuple_flatten(std::get<indices-1>(item))...);
        }

        template <typename T>
        struct tuple_head_helper
        {
            typedef T type;
            static const type& get(const T& item) 
            {
                return item;
            }
        };

        template <typename T, typename... U>
        struct tuple_head_helper<std::tuple<T, U...>>
        {
            typedef typename tuple_head_helper<T>::type type;
            static const type& get(const std::tuple<T,U...>& item) 
            {
                return tuple_head_helper<T>::get(std::get<0>(item));
            }
        };

        template <typename T> struct alwaysbool { typedef bool type; };

        resizable_tensor& rt();

        // The significance of a layer's backward method requiring forward's outputs is
        // that such as layer can't have an in-place layer stacked on top of it because
        // in-place layers overwrite the output of the layer they sit on top of.
        template <typename layer_type, typename SUBNET>
        constexpr auto backward_requires_forward_output(
            layer_type& layer,
            SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.backward(rt(),rt(),sub,rt()))>::type
        {
            return true;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto backward_requires_forward_output(
            layer_type& layer,
            SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.backward(rt(),sub,rt()))>::type
        {
            return false;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto backward_requires_forward_output(
            layer_type& layer,
            SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.backward_inplace(rt(),rt(),sub.get_gradient_input(),rt()))>::type
        {
            return true;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto backward_requires_forward_output(
            layer_type& layer,
            SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.backward_inplace(rt(),sub.get_gradient_input(),rt()))>::type
        {
            return false;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto has_inplace_backward(
            layer_type& layer,
            SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.backward(rt(),rt(),sub,rt()))>::type
        {
            return false;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto has_inplace_backward(
            layer_type& layer,
            SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.backward(rt(),sub,rt()))>::type
        {
            return false;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto has_inplace_backward(
            layer_type& layer,
            SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.backward_inplace(rt(),rt(),sub.get_gradient_input(),rt()))>::type
        {
            return true;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto has_inplace_backward(
            layer_type& layer,
            SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.backward_inplace(rt(),sub.get_gradient_input(),rt()))>::type
        {
            return true;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto is_inplace_layer(
            layer_type& layer,
            const SUBNET& sub 
        ) -> typename alwaysbool<decltype(layer.forward(sub,rt()))>::type
        {
            return false;
        }

        template <typename layer_type, typename SUBNET>
        constexpr auto is_inplace_layer(
            layer_type& layer,
            const SUBNET& sub
        ) -> typename alwaysbool<decltype(layer.forward_inplace(sub.get_output(),rt()))>::type
        {
            return true;
        }

        template <typename layer_type, typename SUBNET>
        auto call_layer_backward(
            layer_type& layer,
            const tensor& computed_output, 
            const tensor& gradient_input, 
            SUBNET& sub, 
            tensor& params_grad
        ) -> decltype(layer.backward(computed_output,gradient_input,sub,params_grad))
        {
            layer.backward(computed_output,gradient_input,sub,params_grad);
        }

        template <typename layer_type, typename SUBNET>
        auto call_layer_backward(
            layer_type& layer,
            const tensor& , 
            const tensor& gradient_input, 
            SUBNET& sub, 
            tensor& params_grad
        ) -> decltype(layer.backward(gradient_input,sub,params_grad))
        {
            layer.backward(gradient_input,sub,params_grad);
        }

        template <typename layer_type, typename SUBNET>
        auto call_layer_backward(
            layer_type& layer,
            const tensor& computed_output, 
            const tensor& gradient_input, 
            SUBNET& sub, 
            tensor& params_grad
        ) -> decltype(layer.backward_inplace(computed_output,gradient_input,sub.get_gradient_input(),params_grad))
        {
            layer.backward_inplace(computed_output,gradient_input,sub.get_gradient_input(),params_grad);
        }

        template <typename layer_type, typename SUBNET>
        auto call_layer_backward(
            layer_type& layer,
            const tensor& , 
            const tensor& gradient_input, 
            SUBNET& sub, 
            tensor& params_grad
        ) -> decltype(layer.backward_inplace(gradient_input,sub.get_gradient_input(),params_grad))
        {
            layer.backward_inplace(gradient_input,sub.get_gradient_input(),params_grad);
        }


        template <typename layer_type, typename SUBNET>
        auto call_layer_forward(
            layer_type& layer,
            const SUBNET& sub, 
            tensor& /*data_output*/
        ) -> decltype(layer.forward(sub,rt()))
        {
            // This overload of call_layer_forward() is here because this template
            // naturally gets instantiated but only on code paths that never get executed.
            // So rather than writing a bunch of hard to read template magic around call
            // sites we just have this overload that doesn't do anything (and an assert to
            // make sure that's the case).
            DLIB_CASSERT(false, "This should never happen");
        }

        template <typename layer_type, typename SUBNET>
        auto call_layer_forward(
            layer_type& layer,
            const SUBNET& sub, 
            resizable_tensor& data_output
        ) -> decltype(layer.forward(sub,data_output))
        {
            layer.forward(sub,data_output);
        }

        template <typename layer_type, typename SUBNET>
        auto call_layer_forward(
            layer_type& layer,
            const SUBNET& sub, 
            tensor& data_output
        ) -> decltype(layer.forward_inplace(sub.get_output(),data_output))
        {
            layer.forward_inplace(sub.get_output(),data_output);
        }

        template <typename layer_type, typename SUBNET>
        auto call_layer_forward(
            layer_type& layer,
            const SUBNET& sub, 
            resizable_tensor& data_output
        ) -> decltype(layer.forward_inplace(sub.get_output(),data_output))
        {
            if (!have_same_dimensions(data_output, sub.get_output()))
                data_output.copy_size(sub.get_output());
            layer.forward_inplace(sub.get_output(),data_output);
        }


    } // end namespace impl

    template <typename... T>
    typename impl::tuple_head_helper<std::tuple<T...>>::type tuple_head (
        const std::tuple<T...>& item
    ) 
    {
        return impl::tuple_head_helper<std::tuple<T...>>::get(item);
    }

    template <typename... T>
    auto tuple_tail(
        const std::tuple<T...>& item
    ) -> decltype(impl::basic_tuple_tail(impl::tuple_flatten(item)))
    {
        return impl::basic_tuple_tail(impl::tuple_flatten(item));
    }

    inline std::tuple<> tuple_tail(
        const std::tuple<>& item
    ) 
    {
        return item;
    }
// ----------------------------------------------------------------------------------------

    inline void randomize_parameters (
        tensor& params,
        unsigned long num_inputs_and_outputs,
        dlib::rand& rnd
    )
    {
        for (auto& val : params)
        {
            // Draw a random number to initialize the layer according to formula (16)
            // from Understanding the difficulty of training deep feedforward neural
            // networks by Xavier Glorot and Yoshua Bengio.
            val = 2*rnd.get_random_float()-1;
            val *= std::sqrt(6.0/(num_inputs_and_outputs));
        }
    }

// ----------------------------------------------------------------------------------------

    template <typename T>
    class sstack
    {
    public:
        typedef T value_type;

        sstack() = delete;

        sstack (
            T* data_,
            size_t s
        ) : data(data_), mysize(s) {}

        const T& top() const 
        { 
            DLIB_CASSERT(size() != 0, "You can't call top() on an empty stack");
            return *data;
        }
        T& top()  
        { 
            DLIB_CASSERT(size() != 0, "You can't call top() on an empty stack");
            return *data;
        }

        size_t size() const { return mysize; }

        sstack pop(size_t num=1) 
        { 
            DLIB_CASSERT(num <= size(), "You can't pop more things from the stack than it has in it.");
            return sstack(data+num, mysize-num);
        }

    private:

        T* data;
        size_t mysize;
    };

    template <typename T>
    sstack<T> make_sstack(std::vector<T>& item)
    {
        return sstack<T>(item.data(), item.size());
    }

// ----------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------

    namespace dimpl
    {
        template <typename T, bool is_first = true, typename enabled=void>
        class subnet_wrapper
        {
            /*!
                WHAT THIS OBJECT REPRESENTS
                    This is a tool that makes an add_layer or add_loss_layer object
                    expose only the part of its interface defined by the SUBNET
                    type in layers_abstract.h.  This way, when we pass subnetwork
                    objects to the layer callbacks those callbacks won't be able to 
                    interact with the subnetworks in a way other than specified 
                    by the SUBNET interface spec.

                    We also allow the top layer of a subnet_wrapper stack to call the
                    private_get_output() and private_get_gradient_input() functions.  This
                    way, layers that have had their output/gradient overwritten by in-place
                    layers can only be accessed from the in-place layers that sit directly
                    on top of them since those in-place layers are the only layers that
                    know how to interact with them properly.
            !*/

        public:
            subnet_wrapper(const subnet_wrapper&) = delete;
            subnet_wrapper& operator=(const subnet_wrapper&) = delete;

            subnet_wrapper(T& l_) {}
            // Nothing here because in this case T is one of the input layer types 
            // that doesn't have anything in it.
        };

        template <typename T>
        class subnet_wrapper<T,true, typename std::enable_if<is_nonloss_layer_type<T>::value>::type>
        {

        public:
            subnet_wrapper(const subnet_wrapper&) = delete;
            subnet_wrapper& operator=(const subnet_wrapper&) = delete;

            typedef T wrapped_type;
            const static size_t num_computational_layers = T::num_computational_layers;

            subnet_wrapper(T& l_) : l(l_),subnetwork(l.subnet()) {}

            const tensor& get_output() const { return l.private_get_output(); }
            tensor& get_gradient_input() { return l.private_get_gradient_input(); }

            const subnet_wrapper<typename T::subnet_type,false>& subnet() const { return subnetwork; }
            subnet_wrapper<typename T::subnet_type,false>& subnet() { return subnetwork; }

        private:
            T& l;
            subnet_wrapper<typename T::subnet_type,false> subnetwork;
        };

        template <typename T>
        class subnet_wrapper<T,false, typename std::enable_if<is_nonloss_layer_type<T>::value>::type>
        {

        public:
            subnet_wrapper(const subnet_wrapper&) = delete;
            subnet_wrapper& operator=(const subnet_wrapper&) = delete;

            typedef T wrapped_type;
            const static size_t num_computational_layers = T::num_computational_layers;

            subnet_wrapper(T& l_) : l(l_),subnetwork(l.subnet()) {}

            const tensor& get_output() const { return l.get_output(); }
            tensor& get_gradient_input() { return l.get_gradient_input(); }

            const subnet_wrapper<typename T::subnet_type,false>& subnet() const { return subnetwork; }
            subnet_wrapper<typename T::subnet_type,false>& subnet() { return subnetwork; }

        private:
            T& l;
            subnet_wrapper<typename T::subnet_type,false> subnetwork;
        };
    }

// ----------------------------------------------------------------------------------------

    template <typename LAYER_DETAILS, typename SUBNET, typename enabled = void>
    class add_layer;


    template <typename T, typename U>
    struct is_nonloss_layer_type<add_layer<T,U>> : std::true_type {};

    template <typename LAYER_DETAILS, typename SUBNET>
    class add_layer<LAYER_DETAILS,SUBNET,
            typename std::enable_if<is_nonloss_layer_type<SUBNET>::value>::type>
    {
    public:
        typedef LAYER_DETAILS layer_details_type;
        typedef SUBNET subnet_type;
        typedef typename subnet_type::input_type input_type;
        const static size_t num_layers = subnet_type::num_layers + 1;
        const static size_t num_computational_layers = subnet_type::num_computational_layers + 1;
        const static unsigned int sample_expansion_factor = subnet_type::sample_expansion_factor;

        add_layer(
        ):
            subnetwork(new subnet_type()),
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {
            if (this_layer_operates_inplace())
                subnetwork->disable_output_and_gradient_getters();
        }

        add_layer(const add_layer& item)
        {
            details = item.details;
            subnetwork.reset(new subnet_type(*item.subnetwork));
            this_layer_setup_called = item.this_layer_setup_called;
            gradient_input_is_stale = item.gradient_input_is_stale;
            get_output_and_gradient_input_disabled = item.get_output_and_gradient_input_disabled;
            x_grad = item.x_grad;
            cached_output = item.cached_output; 
            params_grad = item.params_grad; 
            temp_tensor = item.temp_tensor;
        }
        add_layer& operator=(const add_layer& item) { add_layer(item).swap(*this); return *this;}
        add_layer(add_layer&& item) : add_layer() { swap(item); }
        add_layer& operator=(add_layer&& item) { swap(item); return *this; }

        template <typename T, typename U, typename E>
        friend class add_layer;
        template <typename T, bool is_first, typename E>
        friend class dimpl::subnet_wrapper;
        template <unsigned long T, typename U, typename E>
        friend class add_tag_layer;
        template <template<typename> class T, typename U>
        friend class add_skip_layer;
        template <size_t N, template<typename> class L, typename S>
        friend class repeat;

        // Allow copying networks from one to another as long as their corresponding 
        // layers can be constructed from each other.
        template <typename T, typename U, typename E>
        add_layer(
            const add_layer<T,U,E>& item
        ) :
            details(item.layer_details()), 
            subnetwork(new subnet_type(item.subnet())),
            this_layer_setup_called(item.this_layer_setup_called),
            gradient_input_is_stale(item.gradient_input_is_stale),
            get_output_and_gradient_input_disabled(item.get_output_and_gradient_input_disabled),
            x_grad(item.x_grad),
            cached_output(item.cached_output)
        {
            if (this_layer_operates_inplace())
                subnetwork->disable_output_and_gradient_getters();
        }

        template <typename ...T>
        add_layer(
            const LAYER_DETAILS& layer_det, 
            T&& ...args
        ) : 
            details(layer_det), 
            subnetwork(new subnet_type(std::forward<T>(args)...)),
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {
            if (this_layer_operates_inplace())
                subnetwork->disable_output_and_gradient_getters();
        }

        template <typename T, typename ...U>
        struct disable_forwarding_constr 
        {
            const static bool value = std::is_constructible<LAYER_DETAILS,T>::value;
        };
        template <typename ...T, typename ...U>
        struct disable_forwarding_constr<std::tuple<T...>,U...>
        {
            const static bool value = disable_forwarding_constr<typename std::remove_reference<T>::type...>::value;
        };
        template <typename T, typename ...U>
        struct disable_forwarding_constr<std::tuple<T>,U...>
        {
            const static bool value = disable_forwarding_constr<typename std::remove_reference<T>::type>::value;
        };
        template <typename ...U>
        struct disable_forwarding_constr<std::tuple<>,U...>
        {
            const static bool value = true;
        };
        template <typename ...T>
        struct disable_forwarding_constr<add_layer<T...>>
        {
            const static bool value = true;
        };

        template <
            typename ...T,
            typename = typename std::enable_if<!disable_forwarding_constr<typename std::remove_reference<T>::type...>::value>::type
            >
        add_layer(
            T&& ...args
        ) : 
            subnetwork(new subnet_type(std::forward<T>(args)...)),
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {
            if (this_layer_operates_inplace())
                subnetwork->disable_output_and_gradient_getters();
        }

        template <typename ...T>
        add_layer(
            LAYER_DETAILS&& layer_det, 
            T&& ...args
        ) : 
            details(std::move(layer_det)), 
            subnetwork(new subnet_type(std::forward<T>(args)...)),
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {
            if (this_layer_operates_inplace())
                subnetwork->disable_output_and_gradient_getters();
        }

        template <typename ...T, typename LD, typename ...U>
        add_layer(
            const std::tuple<LD,U...>& layer_det, 
            T&& ...args
        ) : 
            details(tuple_head(layer_det)), 
            subnetwork(new subnet_type(tuple_tail(layer_det),std::forward<T>(args)...)),
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {
            if (this_layer_operates_inplace())
                subnetwork->disable_output_and_gradient_getters();
        }

        template <typename ...T, typename LD, typename ...U>
        add_layer(
            std::tuple<>,
            const std::tuple<LD,U...>& layer_det, 
            T&& ...args
        ) : add_layer(layer_det,args...) { }

        add_layer (
            std::tuple<>
        ) : add_layer() {}

        template <typename ...T>
        add_layer(
            std::tuple<>, 
            LAYER_DETAILS&& layer_det, 
            T&& ...args
        ) : add_layer(layer_det, args...) { }

        template <typename input_iterator>
        void to_tensor (
            input_iterator ibegin,
            input_iterator iend,
            resizable_tensor& data
        ) const
        {
            subnetwork->to_tensor(ibegin,iend,data);
        }

        template <typename input_iterator>
        const tensor& operator() (
            input_iterator ibegin,
            input_iterator iend
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            return forward(temp_tensor);
        }


        const tensor& operator() (const input_type& x)
        {
            return (*this)(&x, &x+1);
        }

        const tensor& forward(const tensor& x)
        {
            subnetwork->forward(x);
            const dimpl::subnet_wrapper<subnet_type> wsub(*subnetwork);
            if (!this_layer_setup_called)
            {
                details.setup(wsub);
                this_layer_setup_called = true;
            }
            if (this_layer_operates_inplace())
                impl::call_layer_forward(details, wsub, private_get_output());
            else
                impl::call_layer_forward(details, wsub, cached_output);

            gradient_input_is_stale = true;
            return private_get_output();
        }

    private:
        tensor& private_get_output() const
        { 
            if (const_cast<add_layer&>(*this).this_layer_operates_inplace())
                return subnetwork->private_get_output();
            else
                return const_cast<resizable_tensor&>(cached_output); 
        }
        tensor& private_get_gradient_input() 
        { 
            if (this_layer_operates_inplace())
            {
                return subnetwork->private_get_gradient_input();
            }
            else
            {
                if (gradient_input_is_stale)
                {
                    gradient_input_is_stale = false;
                    x_grad.copy_size(private_get_output());
                    x_grad = 0;
                }
                return x_grad; 
            }
        }
        void disable_output_and_gradient_getters (
        ) { get_output_and_gradient_input_disabled = true; }
    public:
        const tensor& get_output() const 
        { 
            if (get_output_and_gradient_input_disabled)
                throw dlib::error("Accessing this layer's get_output() is disabled because an in-place layer has been stacked on top of it.");
            return private_get_output(); 
        }
        tensor& get_gradient_input() 
        { 
            if (get_output_and_gradient_input_disabled)
                throw dlib::error("Accessing this layer's get_gradient_input() is disabled because an in-place layer has been stacked on top of it.");
            return private_get_gradient_input();
        }

        const tensor& get_final_data_gradient(
        ) const { return subnetwork->get_final_data_gradient(); }

        void back_propagate_error(const tensor& x)
        {
            back_propagate_error(x, private_get_gradient_input());
        }
        void back_propagate_error(const tensor& x, const tensor& gradient_input)
        {
            dimpl::subnet_wrapper<subnet_type> wsub(*subnetwork);
            params_grad.copy_size(details.get_layer_params());
            impl::call_layer_backward(details, private_get_output(),
                gradient_input, wsub, static_cast<tensor&>(params_grad));

            subnetwork->back_propagate_error(x); 

            // zero out get_gradient_input()
            gradient_input_is_stale = true;
        }

        template <typename solver_type>
        void update_parameters(sstack<solver_type> solvers, double learning_rate)
        {
            DLIB_CASSERT(solvers.size()>=num_computational_layers,"");
            // Don't try to adjust the parameters if this layer doesn't have any or the
            // learning rate is disabled for this layer.
            if (params_grad.size() != 0 && get_learning_rate_multiplier(details) != 0)
            {
                const tensor& step = solvers.top()(learning_rate, details, static_cast<const tensor&>(params_grad));
                tt::add(details.get_layer_params(), details.get_layer_params(), step);
            }
            subnetwork->update_parameters(solvers.pop(), learning_rate);
        }

        const tensor& get_parameter_gradient(
        ) const { return params_grad; }

        tensor& get_parameter_gradient (
        ) { return params_grad; }

        const subnet_type& subnet() const { return *subnetwork; }
        subnet_type& subnet() { return *subnetwork; }

        const layer_details_type& layer_details() const { return details; } 
        layer_details_type& layer_details() { return details; } 

        void clean()
        {
            x_grad.clear();
            cached_output.clear();
            params_grad.clear();
            temp_tensor.clear();
            gradient_input_is_stale = true;
            subnetwork->clean();
        }

        friend void serialize(const add_layer& item, std::ostream& out)
        {
            int version = 2;
            serialize(version, out);
            serialize(*item.subnetwork, out);
            serialize(item.details, out);
            serialize(item.this_layer_setup_called, out);
            serialize(item.gradient_input_is_stale, out);
            serialize(item.get_output_and_gradient_input_disabled, out);
            serialize(item.x_grad, out);
            serialize(item.cached_output, out);
            serialize(item.params_grad, out);
        }

        friend void deserialize(add_layer& item, std::istream& in)
        {
            int version = 0;
            deserialize(version, in);
            if (!(1 <= version && version <= 2))
                throw serialization_error("Unexpected version found while deserializing dlib::add_layer.");
            deserialize(*item.subnetwork, in);
            deserialize(item.details, in);
            deserialize(item.this_layer_setup_called, in);
            deserialize(item.gradient_input_is_stale, in);
            deserialize(item.get_output_and_gradient_input_disabled, in);
            deserialize(item.x_grad, in);
            deserialize(item.cached_output, in);
            if (version == 2)
                deserialize(item.params_grad, in);
        }

        friend std::ostream& operator<< (std::ostream& out, const add_layer& item)
        {
            int min_length = 0;
            item.print(out, 0, min_length);
            return out;
        }

        void print (std::ostream& out, unsigned long idx, int& min_length) const
        {
            out << "layer<" << idx << ">\t" << impl::tensor_to_str(private_get_output(), min_length) << layer_details() << "\n";
            subnet().print(out, idx+1, min_length);
        }

    private:

        bool this_layer_operates_inplace(
        ) 
        {
            // This layer can run in-place if it's an in-place capable layer and also if
            // the layer it's on top of doesn't need its own output tensor (since in-place
            // layers overwrite that tensor)
            return impl::is_inplace_layer(details, *subnetwork) && !subnetwork->this_layer_requires_forward_output();
        }
        bool this_layer_requires_forward_output(
        ) 
        {
            return impl::backward_requires_forward_output(details, *subnetwork);
        }

        void swap(add_layer& item)
        {
            std::swap(subnetwork,item.subnetwork);
            std::swap(details, item.details);
            std::swap(this_layer_setup_called, item.this_layer_setup_called);
            std::swap(gradient_input_is_stale, item.gradient_input_is_stale);
            std::swap(get_output_and_gradient_input_disabled, item.get_output_and_gradient_input_disabled);
            std::swap(x_grad, item.x_grad);
            std::swap(cached_output, item.cached_output);
            std::swap(params_grad, item.params_grad);
        }


        LAYER_DETAILS details;
        std::unique_ptr<subnet_type> subnetwork;
        bool this_layer_setup_called;
        bool gradient_input_is_stale;
        bool get_output_and_gradient_input_disabled;
        // Note that if this_layer_operates_inplace()==true then x_grad and cached_output
        // are not used at all.  Instead, this layer uses these variables from the lower
        // layer.
        resizable_tensor x_grad;
        resizable_tensor cached_output; 

        resizable_tensor params_grad; 

        // temp_tensor doesn't logically contribute to the state of this object.  
        // It is here only to prevent it from being reallocated over and over.
        resizable_tensor temp_tensor;

    };

    template <typename T, typename U, typename E>
    struct is_add_layer<add_layer<T,U,E>> : std::true_type {};
    template <typename T, typename U, typename E>
    struct is_add_layer<const add_layer<T,U,E>> : std::true_type {};
    template <typename T, typename U, typename E>
    struct is_add_layer<add_layer<T,U,E>&> : std::true_type {};
    template <typename T, typename U, typename E>
    struct is_add_layer<const add_layer<T,U,E>&> : std::true_type {};

// ----------------------------------------------------------------------------------------

// This version of add_layer handles the special case where the subnetwork being given is
// just an input layer object.
    template <typename LAYER_DETAILS, typename INPUT_LAYER, typename enabled>
    class add_layer
    {
    public:
        typedef LAYER_DETAILS layer_details_type;
        typedef INPUT_LAYER subnet_type;
        typedef typename INPUT_LAYER::input_type input_type;
        const static unsigned int sample_expansion_factor = INPUT_LAYER::sample_expansion_factor;
        const static size_t num_layers = 2;
        const static size_t num_computational_layers = 1;
        static_assert(sample_expansion_factor >= 1,
            "The input layer can't produce fewer output tensors than there are inputs.");

        add_layer(
        ): 
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {}

        add_layer(const add_layer&) = default;
        add_layer(add_layer&& item) : add_layer() { swap(item); }
        add_layer& operator=(const add_layer&) = default;
        add_layer& operator=(add_layer&& item) { swap(item); return *this; }

        template <typename T, typename U, typename E>
        friend class add_layer;
        template <typename T, bool is_first, typename E>
        friend class dimpl::subnet_wrapper;
        template <unsigned long T, typename U, typename E>
        friend class add_tag_layer;
        template <template<typename> class T, typename U>
        friend class add_skip_layer;
        template <size_t N, template<typename> class L, typename S>
        friend class repeat;

        // Allow copying networks from one to another as long as their corresponding 
        // layers can be constructed from each other.
        template <typename T, typename U, typename E>
        add_layer(
            const add_layer<T,U,E>& item
        ):
            input_layer(item.subnet()),
            details(item.layer_details()),
            this_layer_setup_called(item.this_layer_setup_called),
            gradient_input_is_stale(item.gradient_input_is_stale),
            get_output_and_gradient_input_disabled(false),
            x_grad(item.x_grad),
            cached_output(item.cached_output),
            grad_final(item.grad_final)
        {
        }

        add_layer(
            const LAYER_DETAILS& layer_det
        ) : 
            details(layer_det), 
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {}

        add_layer(
            const INPUT_LAYER& il 
        ) : 
            input_layer(il), 
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {}

        add_layer(
            LAYER_DETAILS&& layer_det
        ) : 
            details(std::move(layer_det)), 
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {}

        add_layer(
            LAYER_DETAILS layer_det, 
            INPUT_LAYER il
        ) : 
            details(std::move(layer_det)),
            input_layer(std::move(il)),
            this_layer_setup_called(false),
            gradient_input_is_stale(true),
            get_output_and_gradient_input_disabled(false)
        {}

        add_layer(
            std::tuple<>,
            const LAYER_DETAILS& layer_det
        ) : add_layer(layer_det) {}

        add_layer(
            std::tuple<>,
            LAYER_DETAILS&& layer_det
        ) : add_layer(layer_det) {}

        add_layer(
            std::tuple<>,
            LAYER_DETAILS layer_det, 
            INPUT_LAYER il
        ) : add_layer(layer_det,il) {}

        add_layer(
            const std::tuple<LAYER_DETAILS>& layer_det
        ) : add_layer(tuple_head(layer_det)) {}

        add_layer(
            const std::tuple<LAYER_DETAILS>& layer_det,
            INPUT_LAYER il
        ) : add_layer(tuple_head(layer_det),il) {}

        template <typename input_iterator>
        void to_tensor (
            input_iterator ibegin,
            input_iterator iend,
            resizable_tensor& data
        ) const
        {
            input_layer.to_tensor(ibegin, iend, data);
            // make sure the input layer's to_tensor() function is implemented properly.
            DLIB_CASSERT(std::distance(ibegin,iend)*sample_expansion_factor == data.num_samples(),"");
            data.async_copy_to_device();
        }


        template <typename input_iterator>
        const tensor& operator() (
            input_iterator ibegin,
            input_iterator iend
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            return forward(temp_tensor);
        }


        const tensor& operator() (const input_type& x)
        {
            return (*this)(&x, &x+1);
        }

        const tensor& forward (const tensor& x)
        {
            DLIB_CASSERT(x.num_samples()%sample_expansion_factor == 0,"");
            subnet_wrapper wsub(x, grad_final);
            if (!this_layer_setup_called)
            {
                details.setup(wsub);
                this_layer_setup_called = true;
            }
            impl::call_layer_forward(details, wsub, cached_output);
            gradient_input_is_stale = true;
            return private_get_output();
        }

    private:
        tensor& private_get_output() const { return const_cast<resizable_tensor&>(cached_output); }
        tensor& private_get_gradient_input() 
        { 
            if (gradient_input_is_stale)
            {
                gradient_input_is_stale = false;
                x_grad.copy_size(private_get_output());
                x_grad = 0;
            }
            return x_grad; 
        }
        void disable_output_and_gradient_getters (
        ) { get_output_and_gradient_input_disabled = true; }
    public:
        const tensor& get_output() const 
        { 
            if (get_output_and_gradient_input_disabled)
                throw dlib::error("Accessing this layer's get_output() is disabled because an in-place layer has been stacked on top of it.");
            return private_get_output(); 
        }
        tensor& get_gradient_input() 
        { 
            if (get_output_and_gradient_input_disabled)
                throw dlib::error("Accessing this layer's get_gradient_input() is disabled because an in-place layer has been stacked on top of it.");
            return private_get_gradient_input();
        }

        const tensor& get_final_data_gradient(
        ) const { return grad_final; }

        void back_propagate_error(const tensor& x)
        {
            back_propagate_error(x, private_get_gradient_input());
        }
        void back_propagate_error(const tensor& x, const tensor& gradient_input)
        {
            // make sure grad_final is initialized to 0
            if (!have_same_dimensions(x, grad_final))
                grad_final.copy_size(x);
            grad_final = 0;  

            subnet_wrapper wsub(x, grad_final);
            params_grad.copy_size(details.get_layer_params());
            impl::call_layer_backward(details, private_get_output(),
                gradient_input, wsub, static_cast<tensor&>(params_grad));

            // zero out get_gradient_input()
            gradient_input_is_stale = true;
        }

        template <typename solver_type>
        void update_parameters(sstack<solver_type> solvers, double learning_rate)
        {
            DLIB_CASSERT(solvers.size()>=num_computational_layers,"");
            // Don't try to adjust the parameters if this layer doesn't have any or the
            // learning rate is disabled for this layer.
            if (params_grad.size() != 0 && get_learning_rate_multiplier(details) != 0) 
            {
                const tensor& step = solvers.top()(learning_rate, details, static_cast<const tensor&>(params_grad));
                tt::add(details.get_layer_params(), details.get_layer_params(), step);
            }
        }

        const tensor& get_parameter_gradient(
        ) const { return params_grad; }

        tensor& get_parameter_gradient (
        )  { return params_grad; }

        const subnet_type& subnet() const { return input_layer; } 
        subnet_type& subnet() { return input_layer; } 

        const layer_details_type& layer_details() const { return details; } 
        layer_details_type& layer_details() { return details; } 

        void clean()
        {
            x_grad.clear();
            grad_final.clear();
            cached_output.clear();
            params_grad.clear();
            temp_tensor.clear();
            gradient_input_is_stale = true;
        }

        friend void serialize(const add_layer& item, std::ostream& out)
        {
            int version = 2;
            serialize(version, out);
            serialize(item.input_layer, out);
            serialize(item.details, out);
            serialize(item.this_layer_setup_called, out);
            serialize(item.gradient_input_is_stale, out);
            serialize(item.get_output_and_gradient_input_disabled, out);
            serialize(item.x_grad, out);
            serialize(item.cached_output, out);
            serialize(item.grad_final, out);
        }

        friend void deserialize(add_layer& item, std::istream& in)
        {
            int version = 0;
            deserialize(version, in);
            if (version != 2)
                throw serialization_error("Unexpected version found while deserializing dlib::add_layer.");
            deserialize(item.input_layer, in);
            deserialize(item.details, in);
            deserialize(item.this_layer_setup_called, in);
            deserialize(item.gradient_input_is_stale, in);
            deserialize(item.get_output_and_gradient_input_disabled, in);
            deserialize(item.x_grad, in);
            deserialize(item.cached_output, in);
            deserialize(item.grad_final, in);
        }

        friend std::ostream& operator<< (std::ostream& out, const add_layer& item)
        {
            int min_length = 0;
            item.print(out, 0, min_length);
            return out;
        }

        void print (std::ostream& out, unsigned long idx, int& min_length) const
        {
            out << "layer<" << idx << ">\t" << impl::tensor_to_str(private_get_output(), min_length) << layer_details() << "\n";

            // Don't print the repeat_input_layer since it doesn't exist from the user's
            // point of view.  It's just an artifact of how repeat<> works.
            if (!std::is_same<subnet_type, impl::repeat_input_layer>::value)
                out << "layer<" << idx+1 << ">\t" << subnet() << "\n";
        }

    private:

        bool this_layer_requires_forward_output(
        ) 
        {
            subnet_wrapper wsub(grad_final, grad_final);
            return impl::backward_requires_forward_output(details, wsub);
        }

        class subnet_wrapper
        {
        public:
            subnet_wrapper(const tensor& x_, resizable_tensor& grad_final_) :
                x(x_), grad_final(grad_final_) {}

            subnet_wrapper(const subnet_wrapper&) = delete;
            subnet_wrapper& operator=(const subnet_wrapper&) = delete;

            const tensor& get_output() const { return x; }
            tensor& get_gradient_input() 
            { 
                if (!have_same_dimensions(x, grad_final))
                {
                    grad_final.copy_size(x);
                    grad_final = 0;  
                }
                return grad_final; 
            }

        private:
            const tensor& x;
            resizable_tensor& grad_final;
        };

        void swap(add_layer& item)
        {
            std::swap(input_layer, item.input_layer);
            std::swap(details, item.details);
            std::swap(this_layer_setup_called, item.this_layer_setup_called);
            std::swap(gradient_input_is_stale, item.gradient_input_is_stale);
            std::swap(get_output_and_gradient_input_disabled, item.get_output_and_gradient_input_disabled);
            std::swap(x_grad, item.x_grad); 
            std::swap(cached_output, item.cached_output); 
            std::swap(grad_final, item.grad_final); 
        }

        subnet_type input_layer;
        LAYER_DETAILS details;
        bool this_layer_setup_called;
        bool gradient_input_is_stale;
        bool get_output_and_gradient_input_disabled;
        resizable_tensor x_grad; 
        resizable_tensor cached_output; 
        resizable_tensor grad_final;

        // The following 2 objects don't logically contribute to the state of this class.
        // They are only here to prevent them from being reallocated over and over in
        // member functions.
        resizable_tensor params_grad; 
        resizable_tensor temp_tensor; 
    };

// ----------------------------------------------------------------------------------------

    template <unsigned long ID, typename SUBNET, typename enabled=void>
    class add_tag_layer;

    template <unsigned long ID, typename SUBNET>
    class add_tag_layer<ID,SUBNET,
            typename std::enable_if<is_nonloss_layer_type<SUBNET>::value>::type>
    {
    public:
        typedef SUBNET subnet_type;
        typedef typename subnet_type::input_type input_type;
        const static size_t num_layers = subnet_type::num_layers + 1;
        const static size_t num_computational_layers = subnet_type::num_computational_layers;
        const static unsigned int sample_expansion_factor = subnet_type::sample_expansion_factor;
        static_assert(sample_expansion_factor >= 1,
            "The input layer can't produce fewer output tensors than there are inputs.");

        add_tag_layer() = default;
        add_tag_layer(const add_tag_layer&) = default;
        add_tag_layer(add_tag_layer&&) = default;
        add_tag_layer& operator=(add_tag_layer&&) = default;
        add_tag_layer& operator=(const add_tag_layer&) = default;

        template <typename T>
        add_tag_layer(
            const add_tag_layer<ID,T>& item
        ) : subnetwork(item.subnet())
        {}

        template <typename ...T>
        add_tag_layer(
            T ...args
        ) : 
            subnetwork(std::move(args)...) 
        {
        }

        template <typename input_iterator>
        void to_tensor (
            input_iterator ibegin,
            input_iterator iend,
            resizable_tensor& data
        ) const
        {
            subnetwork.to_tensor(ibegin,iend,data);
        }

        template <typename input_iterator>
        const tensor& operator() (
            input_iterator ibegin,
            input_iterator iend
        )
        {
            return subnetwork(ibegin,iend);
        }

        const tensor& operator() (const input_type& x)
        {
            return subnetwork(x);
        }

        const tensor& forward(const tensor& x)
        {
            return subnetwork.forward(x);
        }

        const tensor& get_output() const { return subnetwork.get_output(); }

        tensor& get_gradient_input() 
        { 
            return subnetwork.get_gradient_input();
        }

        const tensor& get_final_data_gradient(
        ) const { return subnetwork.get_final_data_gradient(); }

        void back_propagate_error(const tensor& x)
        {
            subnetwork.back_propagate_error(x);
        }
        void back_propagate_error(const tensor& x, const tensor& gradient_input)
        {
            subnetwork.back_propagate_error(x,gradient_input);
        }

        template <typename solver_type>
        void update_parameters(sstack<solver_type> solvers, double learning_rate)
        {
            subnetwork.update_parameters(solvers, learning_rate);
        }

        const tensor& get_parameter_gradient(
        ) const { return params_grad; }

        tensor& get_parameter_gradient (
        ) { return params_grad; }

        const subnet_type& subnet() const { return subnetwork; }
        subnet_type& subnet() { return subnetwork; }

        void clean()
        {
            subnetwork.clean();
        }

        friend void serialize(const add_tag_layer& item, std::ostream& out)
        {
            int version = 1;
            serialize(version, out);
            serialize(item.subnetwork, out);
        }

        friend void deserialize(add_tag_layer& item, std::istream& in)
        {
            int version = 0;
            deserialize(version, in);
            if (version != 1)
                throw serialization_error("Unexpected version found while deserializing dlib::add_tag_layer.");
            deserialize(item.subnetwork, in);
        }

        friend std::ostream& operator<< (std::ostream& out, const add_tag_layer& item)
        {
            int min_length = 0;
            item.print(out, 0, min_length);
            return out;
        }

        void print (std::ostream& out, unsigned long idx, int& min_length) const
        {
            out << "layer<" << idx << ">\t" << impl::tensor_to_str(private_get_output(), min_length) << "tag" << ID << "\n";
            subnet().print(out, idx+1, min_length);
        }

    private:

        template <typename T, typename U, typename E>
        friend class add_layer;
        template <typename T, bool is_first, typename E>
        friend class dimpl::subnet_wrapper;
        template <unsigned long T, typename U, typename E>
        friend class add_tag_layer;
        template <template<typename> class T, typename U>
        friend class add_skip_layer;
        template <size_t N, template<typename> class L, typename S>
        friend class repeat;

        // You wouldn't put a tag on a layer if you didn't want to access its forward
        // outputs.  So this is always true.
        bool this_layer_requires_forward_output(
        ) { return true; } 

        void disable_output_and_gradient_getters (
        ) 
        { 
            // This should never happen because only inplace layers call
            // disable_output_and_gradient_getters(), however, putting a tag layer right
            // before an inplace layer basically means you don't want the following layer
            // to operate in place.  So the inplace layer should turn itself into an
            // out-of-place layer and not call disable_output_and_gradient_getters(). 
            DLIB_CASSERT(false,"This should never happen");
        }

        tensor& private_get_output() const
        { return subnetwork.private_get_output(); }
        tensor& private_get_gradient_input() 
        { return subnetwork.private_get_gradient_input(); }

        subnet_type subnetwork;

        // This member doesn't logically contribute to the state of the object since it is
        // always empty. It's just here so we can have the get_parameter_gradient() methods
        // which have to return something.  So they return this empty tensor.
        resizable_tensor params_grad;
    };

// ----------------------------------------------------------------------------------------

    template <typename ...T>
    struct decorator_repeat_group
    {
        decorator_repeat_group(
            T&& ...args
        ) : data(std::forward<T>(args)...) {}

        std::tuple<T...> data;
    };
    template <typename ...T>
    decorator_repeat_group<T...> repeat_group (
        T&& ...args
    )
    {
        return decorator_repeat_group<T...>(std::forward<T>(args)...);
    }

    template <
        size_t num,
        template<typename> class REPEATED_LAYER, 
        typename SUBNET
        >
    class repeat
    {
        static_assert(num > 0, "You can't have a layer repeated 0 times.");
    public:
        typedef SUBNET subnet_type;
        typedef typename SUBNET::input_type input_type;
        const static size_t comp_layers_in_each_group = (REPEATED_LAYER<SUBNET>::num_computational_layers-SUBNET::num_computational_layers);
        const static size_t comp_layers_in_repeated_group = comp_layers_in_each_group*num;
        const static size_t num_computational_layers = comp_layers_in_repeated_group + SUBNET::num_computational_layers;

        const static size_t layers_in_each_group = (REPEATED_LAYER<SUBNET>::num_layers-SUBNET::num_layers);
        const static size_t layers_in_repeated_group = layers_in_each_group*num;
        const static size_t num_layers = subnet_type::num_layers + layers_in_repeated_group;

        const static unsigned int sample_expansion_factor = SUBNET::sample_expansion_factor;

        typedef REPEATED_LAYER<impl::repeat_input_layer> repeated_layer_type;

        repeat(
        ) : 
            details(num)
        {
        }

        size_t num_repetitions (
        ) const { return num; }

        const repeated_layer_type& get_repeated_layer (
            size_t i 
        ) const
        { 
            DLIB_CASSERT(i < num_repetitions(), "");
            return details[i]; 
        }

        repeated_layer_type& get_repeated_layer (
            size_t i 
        ) 
        { 
            DLIB_CASSERT(i < num_repetitions(), "");
            return details[i]; 
        }

        repeat(const repeat&) = default;
        repeat(repeat&&) = default;
        repeat& operator=(repeat&&) = default;
        repeat& operator=(const repeat&) = default;

        template <template<typename> class T, typename U>
        repeat(
            const repeat<num,T,U>& item
        ) : 
            subnetwork(item.subnetwork)
        {
            for (auto&& d : item.details)
                details.emplace_back(d);
        }

        template <typename T, typename ...U>
        repeat(
            T arg1,
            U ...args2
        ): 
            details(num, std::move(arg1)),
            subnetwork(std::move(args2)...)
        {
        }

        template <typename ...T, typename ...U>
        repeat(
            decorator_repeat_group<T...>&& arg1,
            U ...args2
        ): 
            details(num, arg1.data),
            subnetwork(std::move(args2)...)
        {
        }

        template <typename T, typename ...U>
        repeat(
            std::tuple<>,
            T arg1,
            U ...args2
        ): 
            details(num, std::move(arg1)),
            subnetwork(std::move(args2)...)
        {
        }

        template <typename input_iterator>
        void to_tensor (
            input_iterator ibegin,
            input_iterator iend,
            resizable_tensor& data
        ) const
        {
            subnetwork.to_tensor(ibegin,iend,data);
        }

        template <typename input_iterator>
        const tensor& operator() (
            input_iterator ibegin,
            input_iterator iend
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            return forward(temp_tensor);
        }

        const tensor& operator() (const input_type& x)
        {
            return (*this)(&x, &x+1);
        }

        const tensor& forward(const tensor& x)
        {
            subnetwork.forward(x);
            details[details.size()-1].forward(subnetwork.get_output());
            for (long i = details.size()-2; i >= 0; --i)
                details[i].forward(details[i+1].get_output());
            return private_get_output();
        }

    private:
        tensor& private_get_output() const
        { 
            return details[0].private_get_output();
        }
        tensor& private_get_gradient_input() 
        { 
            return details[0].private_get_gradient_input();
        }
    public:
        const tensor& get_output() const 
        { 
            return details[0].get_output(); 
        }
        tensor& get_gradient_input() 
        { 
            return details[0].get_gradient_input();
        }

        const tensor& get_parameter_gradient(
        ) const { return details[0].get_parameter_gradient(); }

        tensor& get_parameter_gradient (
        ) { return details[0].get_parameter_gradient(); }

        void back_propagate_error(const tensor& x)
        {
            back_propagate_error(x, private_get_gradient_input());
        }
        void back_propagate_error(const tensor& x, const tensor& gradient_input)
        {
            if (details.size() > 1)
            {
                details[0].back_propagate_error(details[1].get_output(), gradient_input);
                for (size_t i = 1; i < details.size(); ++i)
                {
                    if (i+1 < details.size())
                        details[i].back_propagate_error(details[i+1].get_output(), details[i-1].get_final_data_gradient());
                    else
                        details[i].back_propagate_error(subnetwork.get_output(), details[i-1].get_final_data_gradient());
                }
            }
            else
            {
                details[0].back_propagate_error(subnetwork.get_output(), gradient_input);
            }
            subnetwork.back_propagate_error(x, details.back().get_final_data_gradient());
        }

        template <typename solver_type>
        void update_parameters(sstack<solver_type> solvers, double learning_rate)
        {
            for (size_t i = 0; i < details.size(); ++i)
                details[i].update_parameters(solvers.pop(comp_layers_in_each_group*i),learning_rate);
            subnetwork.update_parameters(solvers.pop(comp_layers_in_each_group*details.size()),learning_rate);
        }

        const subnet_type& subnet() const { return subnetwork; }
        subnet_type& subnet() { return subnetwork; }

        void clean()
        {
            temp_tensor.clear();
            subnetwork.clean();
            for (auto&& d : details)
                d.clean();
        }

        friend void serialize(const repeat& item, std::ostream& out)
        {
            int version = 1;
            serialize(version, out);
            serialize(item.details, out);
            serialize(item.subnetwork, out);
        }

        friend void deserialize(repeat& item, std::istream& in)
        {
            int version = 0;
            deserialize(version, in);
            if (version != 1)
                throw serialization_error("Unexpected version found while deserializing dlib::repeat.");
            deserialize(item.details, in);
            deserialize(item.subnetwork, in);
        }

        friend std::ostream& operator<< (std::ostream& out, const repeat& item)
        {
            int min_length = 0;
            item.print(out, 0, min_length);
            return out;
        }

        void print (std::ostream& out, unsigned long idx, int& min_length) const
        {
            for (size_t i = 0; i < num_repetitions(); ++i)
            {
                get_repeated_layer(i).print(out, idx, min_length);
                idx += layers_in_each_group;
            }
            subnet().print(out, idx, min_length);
        }
    private:


        template <typename T, typename U, typename E>
        friend class add_layer;
        template <typename T, bool is_first, typename E>
        friend class dimpl::subnet_wrapper;
        template <unsigned long T, typename U, typename E>
        friend class add_tag_layer;
        template <template<typename> class T, typename U>
        friend class add_skip_layer;
        template <size_t N, template<typename> class L, typename S>
        friend class repeat;

        bool this_layer_requires_forward_output(
        ) 
        { 
            return details[0].this_layer_requires_forward_output(); 
        } 

        void disable_output_and_gradient_getters (
        ) 
        { 
            details[0].disable_output_and_gradient_getters();
        }


        std::vector<repeated_layer_type> details; 
        subnet_type subnetwork;

        // temp_tensor doesn't logically contribute to the state of this class.
        // It is here only to void needing to reallocate it over and over.
        resizable_tensor temp_tensor;
    };

    template <
        size_t num,
        template<typename> class REPEATED_LAYER, 
        typename SUBNET
        >
    struct is_nonloss_layer_type<repeat<num,REPEATED_LAYER,SUBNET>> : std::true_type {};

// ----------------------------------------------------------------------------------------

// This version of add_tag_layer handles the special case where the subnetwork being given
// is just an input layer object.
    template <unsigned long ID, typename INPUT_LAYER, typename enabled>
    class add_tag_layer
    {
    public:
        typedef INPUT_LAYER subnet_type;
        typedef typename subnet_type::input_type input_type;
        const static size_t num_computational_layers = 0;
        const static size_t num_layers = 2;
        const static unsigned int sample_expansion_factor = subnet_type::sample_expansion_factor;
        static_assert(sample_expansion_factor >= 1,
            "The input layer can't produce fewer output tensors than there are inputs.");

        add_tag_layer():cached_output_ptr(nullptr),gradient_input_is_stale(true) {}

        add_tag_layer(const add_tag_layer&) = default;
        add_tag_layer& operator=(const add_tag_layer&) = default;
        add_tag_layer(add_tag_layer&& item) : add_tag_layer() { swap(item); }
        add_tag_layer& operator=(add_tag_layer&& item) { swap(item); return *this; }

        template <typename T, typename E>
        add_tag_layer(
            const add_tag_layer<ID,T,E>& item
        ) : input_layer(item.subnet()), 
            cached_output(item.cached_output),
            cached_output_ptr(nullptr),
            grad_final(item.grad_final),
            gradient_input_is_stale(item.gradient_input_is_stale)
        {}

        template <typename ...T>
        add_tag_layer(
            T ...args
        ) : 
            input_layer(std::move(args)...),
            cached_output_ptr(nullptr),
            gradient_input_is_stale(true)
        {
        }

        add_tag_layer (
            std::tuple<>
        ) : 
            cached_output_ptr(nullptr),
            gradient_input_is_stale(true)
        {}

        template <typename input_iterator>
        void to_tensor (
            input_iterator ibegin,
            input_iterator iend,
            resizable_tensor& data
        ) const
        {
            input_layer.to_tensor(ibegin,iend,data);
        }

        template <typename input_iterator>
        const tensor& operator() (
            input_iterator ibegin, 
            input_iterator iend
        )
        {
            input_layer.to_tensor(ibegin,iend,cached_output);
            cached_output_ptr = nullptr;
            return get_output();
        }

        const tensor& operator() (const input_type& x)
        {
            return (*this)(&x, &x+1);
        }

        const tensor& forward(const tensor& x)
        {
            // If this tag is the first layer in one of the sub networks inside a repeat
            // layer then we don't want it to be creating copies of x.  This is because, we
            // can just hold a pointer to x since the way repeat is constructed guarantees
            // that x will have a lifetime larger than this pointer. 
            if (is_same_type<INPUT_LAYER, impl::repeat_input_layer>::value)
                cached_output_ptr = const_cast<tensor*>(&x);
            else
                cached_output = x;
            gradient_input_is_stale = true;
            return get_output();
        }

        const tensor& get_output() const 
        { 
            if (cached_output_ptr)
                return *cached_output_ptr;
            else
                return cached_output; 
        }

        const tensor& get_final_data_gradient(
        ) const { return grad_final; }

        tensor& get_gradient_input() 
        { 
            if (!have_same_dimensions(get_output(), grad_final) ||
                gradient_input_is_stale)
            {
                grad_final.copy_size(get_output());
                grad_final = 0;
                gradient_input_is_stale = false;
            }
            return grad_final; 
        }

        void back_propagate_error(const tensor& /*x*/)
        {
            // nothing to do
        }
        void back_propagate_error(const tensor& /*x*/, const tensor& /*gradient_input*/)
        {
            // nothing to do
        }

        template <typename solver_type>
        void update_parameters(sstack<solver_type> /*solvers*/, double /*learning_rate*/)
        {
            // nothing to do
        }

        const subnet_type& subnet() const { return input_layer; }
        subnet_type& subnet() { return input_layer; }

        void clean()
        {
            grad_final.clear();
            cached_output.clear();
            cached_output_ptr = 0;
        }

        friend void serialize(const add_tag_layer& item, std::ostream& out)
        {
            int version = 1;
            serialize(version, out);
            serialize(item.input_layer, out);
            serialize(item.cached_output, out);
            serialize(item.grad_final, out);
            serialize(item.gradient_input_is_stale, out);
        }

        friend void deserialize(add_tag_layer& item, std::istream& in)
        {
            int version = 0;
            deserialize(version, in);
            if (version != 1)
                throw serialization_error("Unexpected version found while deserializing dlib::add_tag_layer.");
            deserialize(item.input_layer, in);
            deserialize(item.cached_output, in);
            deserialize(item.grad_final, in);
            deserialize(item.gradient_input_is_stale, in);
            item.cached_output_ptr = nullptr;
        }

        friend std::ostream& operator<< (std::ostream& out, const add_tag_layer& item)
        {
            int min_length = 0;
            item.print(out, 0, min_length);
            return out;
        }

        void print (std::ostream& out, unsigned long idx, int& min_length) const
        {
            out << "layer<"<<idx << ">\t"<<impl::tensor_to_str(private_get_output(), min_length)<< "tag" << ID << "\n";
            // Don't print the repeat_input_layer since it doesn't exist from the user's
            // point of view.  It's just an artifact of how repeat<> works.
            if (!std::is_same<subnet_type, impl::repeat_input_layer>::value)
                out << "layer<"<< idx+1 << ">\t" << subnet() << "\n";
        }

    private:

        template <typename T, typename U, typename E>
        friend class add_layer;
        template <typename T, bool is_first, typename E>
        friend class dimpl::subnet_wrapper;
        template <unsigned long T, typename U, typename E>
        friend class add_tag_layer;
        template <template<typename> class T, typename U>
        friend class add_skip_layer;
        template <size_t N, template<typename> class L, typename S>
        friend class repeat;

        // You woudln't put a tag on a layer if you didn't want to access its forward
        // outputs.  So this is always true.
        bool this_layer_requires_forward_output(
        ) { return true; } 

        void disable_output_and_gradient_getters (
        ) 
        { 
            // This should never happen because only inplace layers call
            // disable_output_and_gradient_getters(), however, putting a tag layer right
            // before an inplace layer basically means you don't want the following layer
            // to operate in place.  So the inplace layer should turn itself into an
            // out-of-place layer and not call disable_output_and_gradient_getters(). 
            DLIB_CASSERT(false,"This should never happen");
        }

        tensor& private_get_output() const
        { return const_cast<tensor&>(get_output()); }
        tensor& private_get_gradient_input() 
        { return get_gradient_input(); }

        void swap(add_tag_layer& item)
        {
            std::swap(input_layer, item.input_layer);
            std::swap(cached_output, item.cached_output);
            std::swap(cached_output_ptr, item.cached_output_ptr);
            std::swap(grad_final, item.grad_final);
            std::swap(gradient_input_is_stale, item.gradient_input_is_stale);
        }

        subnet_type input_layer;
        resizable_tensor cached_output;
        tensor* cached_output_ptr;
        resizable_tensor grad_final;
        bool gradient_input_is_stale;
    };

    template <unsigned long ID, typename U, typename E>
    struct is_nonloss_layer_type<add_tag_layer<ID,U,E>> : std::true_type {};


// ----------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------

    template <typename LOSS_DETAILS, typename SUBNET>
    class add_loss_layer;

    class no_label_type
    {
    private:
        // We don't want anyone making these no_label_type objects.  They are here only to
        // allow add_loss_layer::label_type and dnn_trainer::label_type to exist which avoids
        // needing to overload add_loss_layer and dnn_trainer for supervised an unsupervised
        // losses.  It also can be a type to use in template metaprogramming to indicate
        // "no label".  So here we make the constructor private with the exception that
        // add_loss_layer objects can make it (again, just to simplify add_loss_layer's
        // implementation).
        no_label_type(){};
        template <typename LOSS_DETAILS, typename SUBNET> friend class add_loss_layer;
        template < typename net_type, typename solver_type > friend class dnn_trainer; 
    };

// ----------------------------------------------------------------------------------------

    template <typename LOSS_DETAILS, typename SUBNET>
    class add_loss_layer
    {
        template <typename T, typename enabled=void>
        struct get_loss_layer_label_type
        {
            typedef no_label_type type;
        };
        template <typename T>
        struct get_loss_layer_label_type<T,typename std::enable_if<sizeof(typename T::label_type)!=0>::type>
        {
            typedef typename T::label_type type;
        };

    public:
        typedef LOSS_DETAILS loss_details_type;
        typedef SUBNET subnet_type;
        typedef typename subnet_type::input_type input_type;
        const static size_t num_layers = subnet_type::num_layers + 1;
        // Note that the loss layer doesn't count as an additional computational layer.
        const static size_t num_computational_layers = subnet_type::num_computational_layers;
        const static unsigned int sample_expansion_factor = subnet_type::sample_expansion_factor;
        typedef typename get_loss_layer_label_type<LOSS_DETAILS>::type label_type;

        static_assert(is_nonloss_layer_type<SUBNET>::value, 
            "SUBNET must be of type add_layer, add_skip_layer, or add_tag_layer."); 
        static_assert(sample_expansion_factor == LOSS_DETAILS::sample_expansion_factor,
            "The loss layer and input layer must agree on the sample_expansion_factor.");


        add_loss_layer() {};
        add_loss_layer(const add_loss_layer&) = default;
        add_loss_layer& operator=(const add_loss_layer&) = default;
        add_loss_layer(add_loss_layer&& item) : add_loss_layer() { swap(item); }
        add_loss_layer& operator=(add_loss_layer&& item) { swap(item); return *this; }

        template <typename T, typename U>
        add_loss_layer(
            const add_loss_layer<T,U>& item
        ) : 
            loss(item.loss_details()),
            subnetwork(item.subnet())
        {}

        template <typename ...T>
        add_loss_layer(
            const LOSS_DETAILS& layer_det, 
            T&& ...args
        ) : 
            loss(layer_det), 
            subnetwork(std::forward<T>(args)...)
        {
        }

        template <typename ...T>
        add_loss_layer(
            LOSS_DETAILS&& layer_det, 
            T&& ...args
        ) : 
            loss(std::move(layer_det)), 
            subnetwork(std::forward<T>(args)...)
        {
        }

        template <typename ...T>
        add_loss_layer(
            T ...args
        ) : 
            subnetwork(std::move(args)...)
        {
        }

        template <typename input_iterator>
        void to_tensor (
            input_iterator ibegin,
            input_iterator iend,
            resizable_tensor& data
        ) const
        {
            subnetwork.to_tensor(ibegin,iend,data);
        }

        template <typename output_iterator>
        void operator() (
            const tensor& x, 
            output_iterator obegin
        )
        {
            subnetwork.forward(x);
            const dimpl::subnet_wrapper<subnet_type> wsub(subnetwork);
            loss.to_label(x, wsub, obegin);
        }

        template <typename input_iterator, typename output_iterator>
        void operator() (
            input_iterator ibegin,
            input_iterator iend,
            output_iterator obegin
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            (*this)(temp_tensor, obegin);
        }

        const label_type& operator() (const input_type& x)
        {
            (*this)(&x, &x+1, &temp_label);
            return temp_label;
        }

        template <typename iterable_type>
        std::vector<label_type> operator() (
            const iterable_type& data,
            size_t batch_size = 128
        )
        {
            std::vector<label_type> results(std::distance(data.begin(), data.end()));
            auto o = results.begin();
            for (auto i = data.begin(); i < data.end(); i+=batch_size, o+=batch_size)
            {
                auto end = std::min(i+batch_size, data.end());
                (*this)(i, end, o);
            }
            return results;
        }

        template <typename label_iterator>
        double compute_loss (
            const tensor& x,
            label_iterator lbegin 
        )
        {
            subnetwork.forward(x);
            dimpl::subnet_wrapper<subnet_type> wsub(subnetwork);
            return loss.compute_loss_value_and_gradient(x, lbegin, wsub);
        }

        template <typename input_iterator, typename label_iterator>
        double compute_loss (
            input_iterator ibegin,
            input_iterator iend,
            label_iterator lbegin 
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            return compute_loss(temp_tensor, lbegin);
        }

        double compute_loss (
            const tensor& x
        )
        {
            subnetwork.forward(x);
            dimpl::subnet_wrapper<subnet_type> wsub(subnetwork);
            return loss.compute_loss_value_and_gradient(x, wsub);
        }

        template <typename input_iterator>
        double compute_loss (
            input_iterator ibegin,
            input_iterator iend
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            return compute_loss(temp_tensor);
        }

        template <typename label_iterator>
        double compute_parameter_gradients (
            const tensor& x,
            label_iterator lbegin
        )
        {
            subnetwork.forward(x);
            dimpl::subnet_wrapper<subnet_type> wsub(subnetwork);
            double l = loss.compute_loss_value_and_gradient(x, lbegin, wsub);
            subnetwork.back_propagate_error(x);
            return l;
        }
        template <typename input_iterator, typename label_iterator>
        double compute_parameter_gradients (
            input_iterator ibegin,
            input_iterator iend,
            label_iterator lbegin
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            return compute_parameter_gradients(temp_tensor, lbegin);
        }
        double compute_parameter_gradients (
            const tensor& x
        )
        {
            subnetwork.forward(x);
            dimpl::subnet_wrapper<subnet_type> wsub(subnetwork);
            double l = loss.compute_loss_value_and_gradient(x, wsub);
            subnetwork.back_propagate_error(x);
            return l;
        }
        template <typename input_iterator>
        double compute_parameter_gradients (
            input_iterator ibegin,
            input_iterator iend
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            return compute_parameter_gradients(temp_tensor);
        }

        template <typename solver_type>
        void update_parameters (
            sstack<solver_type> solvers,
            double learning_rate
        )
        {
            subnetwork.update_parameters(solvers, learning_rate);
        }

        const subnet_type& subnet() const { return subnetwork; }
        subnet_type& subnet() { return subnetwork; }
        const loss_details_type& loss_details() const { return loss; }
        loss_details_type& loss_details() { return loss; }

        void clean (
        )
        {
            temp_tensor.clear();
            subnetwork.clean();
        }

        friend void serialize(const add_loss_layer& item, std::ostream& out)
        {
            int version = 1;
            serialize(version, out);
            serialize(item.loss, out);
            serialize(item.subnetwork, out);
        }

        friend void deserialize(add_loss_layer& item, std::istream& in)
        {
            int version = 0;
            deserialize(version, in);
            if (version != 1)
                throw serialization_error("Unexpected version found while deserializing dlib::add_loss_layer.");
            deserialize(item.loss, in);
            deserialize(item.subnetwork, in);
        }

        friend std::ostream& operator<< (std::ostream& out, const add_loss_layer& item)
        {
            int min_length = 0;
            item.print(out, 0, min_length);
            return out;
        }

        void print (std::ostream& out, unsigned long idx, int& min_length) const
        {
            out << "layer<" << idx << ">\t" << loss_details() << "\n";
            subnet().print(out, idx+1, min_length);
        }

    private:


        void swap(add_loss_layer& item)
        {
            std::swap(loss, item.loss);
            std::swap(subnetwork, item.subnetwork);
        }

        loss_details_type loss;
        subnet_type subnetwork;

        // These two objects don't logically contribute to the state of this object.  They
        // are here to prevent them from being reallocated over and over.
        label_type temp_label;
        resizable_tensor temp_tensor;
    };


    template <typename T, typename U>
    struct is_loss_layer_type<add_loss_layer<T,U>> : std::true_type {};

// ----------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------
// ----------------------------------------------------------------------------------------

    namespace impl
    {
        template <unsigned int i, typename T, typename enabled = void>
        struct layer_helper
        {
            static_assert(i < T::num_layers, "Call to layer() attempted to access non-existing layer in neural network.");
            static T& makeT();
            using next_type = typename std::remove_reference<decltype(makeT().subnet())>::type;
            using type = typename layer_helper<i-1,next_type>::type;
            static type& layer(T& n)
            {
                return layer_helper<i-1,next_type>::layer(n.subnet());
            }
        };
        template <
            unsigned int i,
            size_t N, template<typename> class L, typename S
        >
        struct layer_helper<i,repeat<N,L,S>, typename std::enable_if<(i!=0&&i>=repeat<N,L,S>::layers_in_repeated_group)>::type>
        {
            const static size_t layers_in_repeated_group = repeat<N,L,S>::layers_in_repeated_group;

            static repeat<N,L,S>& makeT();
            using next_type = typename std::remove_reference<decltype(makeT().subnet())>::type;
            using type = typename layer_helper<i-layers_in_repeated_group,next_type>::type;
            static type& layer(repeat<N,L,S>& n)
            {
                return layer_helper<i-layers_in_repeated_group,next_type>::layer(n.subnet());
            }
        };
        template <
            unsigned int i,
            size_t N, template<typename> class L, typename S
        >
        struct layer_helper<i,repeat<N,L,S>, typename std::enable_if<(i!=0&&i<repeat<N,L,S>::layers_in_repeated_group)>::type>
        {
            const static size_t layers_in_each_group = repeat<N,L,S>::layers_in_each_group;
            typedef typename repeat<N,L,S>::repeated_layer_type repeated_layer_type;
            using next_type = repeated_layer_type;
            using type = typename layer_helper<i%layers_in_each_group,next_type>::type;
            static type& layer(repeat<N,L,S>& n)
            {
                return layer_helper<i%layers_in_each_group,next_type>::layer(n.get_repeated_layer(i/layers_in_each_group));
            }
        };
        template <
            size_t N, template<typename> class L, typename S
        >
        struct layer_helper<0,repeat<N,L,S>, void>
        {
            typedef typename repeat<N,L,S>::repeated_layer_type repeated_layer_type;
            using type = repeated_layer_type;
            static type& layer(repeat<N,L,S>& n)
            {
                return n.get_repeated_layer(0);
            }
        };
        template <typename T>
        struct layer_helper<0,T,void>
        {
            using type = T;
            static type& layer(T& n)
            {
                return n;
            }
        };

        template <template<typename> class Match, typename T, unsigned int i, typename enabled = void>
        struct layer_helper_match
        {
            static T& makeT();
            using next_type = typename std::remove_reference<decltype(makeT().subnet())>::type;
            using type = typename layer_helper_match<Match,next_type,i>::type;
            static type& layer(T& n)
            {
                return layer_helper_match<Match,next_type,i>::layer(n.subnet());
            }
        };
        // This overload catches add_layer and add_loss_layer templates.
        template <template<typename> class Match, typename T, unsigned int i>
        struct layer_helper_match<Match,T,i,
            typename std::enable_if<std::is_same<const T,const  Match<typename T::subnet_type>>::value>::type>
        {
            using type = typename layer_helper<i,T>::type;
            static type& layer(T& n)
            {
                return layer_helper<i,T>::layer(n);
            }
        };
        // This overload catches input templates.
        template <template<typename> class Match, typename T, unsigned int i>
        struct layer_helper_match<Match,T,i,
            typename std::enable_if<std::is_same<const T,const  Match<typename T::input_type>>::value>::type>
        {
            using type = typename layer_helper<i,T>::type;
            static type& layer(T& n)
            {
                return layer_helper<i,T>::layer(n);
            }
        };
        // This overload catches subnet_wrapper templates.
        template <template<typename> class Match, typename T, unsigned int i>
        struct layer_helper_match<Match,T,i,
            typename std::enable_if<std::is_same<const typename T::wrapped_type, 
                                                 const Match<typename T::wrapped_type::subnet_type>>::value>::type>
        {
            using type = typename layer_helper<i,T>::type;
            static type& layer(T& n)
            {
                return layer_helper<i,T>::layer(n);
            }
        };
    }

    template <unsigned int i, typename T>
    typename impl::layer_helper<i,T>::type& layer (T& n) 
    {
        return impl::layer_helper<i,T>::layer(n);
    }

    template <template<typename> class Match, typename T>
    typename impl::layer_helper_match<Match,T,0>::type& layer (T& n) 
    {
        return impl::layer_helper_match<Match,T,0>::layer(n);
    }

    template <template<typename> class Match, unsigned int i, typename T>
    typename impl::layer_helper_match<Match,T,i>::type& layer (T& n) 
    {
        return impl::layer_helper_match<Match,T,i>::layer(n);
    }

// ----------------------------------------------------------------------------------------

    template <template<typename> class TAG_TYPE, typename SUBNET>
    class add_skip_layer
    {
    public:
        typedef SUBNET subnet_type;
        typedef typename subnet_type::input_type input_type;
        const static size_t num_layers = subnet_type::num_layers + 1;
        const static size_t num_computational_layers = subnet_type::num_computational_layers;
        const static unsigned int sample_expansion_factor = subnet_type::sample_expansion_factor;
        static_assert(sample_expansion_factor >= 1,
            "The input layer can't produce fewer output tensors than there are inputs.");

        add_skip_layer() = default;
        add_skip_layer(const add_skip_layer&) = default;
        add_skip_layer(add_skip_layer&&) = default;
        add_skip_layer& operator=(add_skip_layer&&) = default;
        add_skip_layer& operator=(const add_skip_layer&) = default;

        template <typename T>
        add_skip_layer(
            const add_skip_layer<TAG_TYPE,T>& item
        ) : subnetwork(item.subnet())
        {}

        template <typename ...T>
        add_skip_layer(
            T ...args
        ) : 
            subnetwork(std::move(args)...) 
        {
        }

        template <typename input_iterator>
        void to_tensor (
            input_iterator ibegin,
            input_iterator iend,
            resizable_tensor& data
        ) const
        {
            subnetwork.to_tensor(ibegin,iend,data);
        }

        template <typename input_iterator>
        const tensor& operator() (
            input_iterator ibegin,
            input_iterator iend
        )
        {
            subnetwork(ibegin,iend);
            return layer<TAG_TYPE>(subnetwork).get_output();
        }

        const tensor& operator() (const input_type& x)
        {
            subnetwork(x);
            return layer<TAG_TYPE>(subnetwork).get_output();
        }

        const tensor& forward(const tensor& x)
        {
            subnetwork.forward(x);
            return layer<TAG_TYPE>(subnetwork).get_output();
        }

        const tensor& get_output() const 
        { 
            return layer<TAG_TYPE>(subnetwork).get_output();
        }

        tensor& get_gradient_input() 
        { 
            return layer<TAG_TYPE>(subnetwork).get_gradient_input();
        }

        const tensor& get_final_data_gradient(
        ) const 
        { 
            return subnetwork.get_final_data_gradient(); 
        }

        void back_propagate_error(const tensor& x)
        {
            subnetwork.back_propagate_error(x);
        }

        template <typename solver_type>
        void update_parameters(sstack<solver_type> solvers, double learning_rate)
        {
            subnetwork.update_parameters(solvers, learning_rate);
        }

        const tensor& get_parameter_gradient(
        ) const { return params_grad; }

        tensor& get_parameter_gradient (
        ) { return params_grad; }


        const subnet_type& subnet() const 
        { 
            return subnetwork; 
        }

        subnet_type& subnet() 
        { 
            return subnetwork; 
        }

        void clean()
        {
            subnetwork.clean();
        }

        friend void serialize(const add_skip_layer& item, std::ostream& out)
        {
            int version = 1;
            serialize(version, out);
            serialize(item.subnetwork, out);
        }

        friend void deserialize(add_skip_layer& item, std::istream& in)
        {
            int version = 0;
            deserialize(version, in);
            if (version != 1)
                throw serialization_error("Unexpected version found while deserializing dlib::add_skip_layer.");
            deserialize(item.subnetwork, in);
        }

        friend std::ostream& operator<< (std::ostream& out, const add_skip_layer& item)
        {
            int min_length = 0;
            item.print(out, 0, min_length);
            return out;
        }

        void print (std::ostream& out, unsigned long idx, int& min_length) const
        {
            out << "layer<" << idx << ">\t"<<impl::tensor_to_str(private_get_output(), min_length) <<"skip\n";
            subnet().print(out, idx+1, min_length);
        }

    private:


        template <typename T, typename U, typename E>
        friend class add_layer;
        template <typename T, bool is_first, typename E>
        friend class dimpl::subnet_wrapper;
        template <unsigned long T, typename U, typename E>
        friend class add_tag_layer;
        template <template<typename> class T, typename U>
        friend class add_skip_layer;
        template <size_t N, template<typename> class L, typename S>
        friend class repeat;

        bool this_layer_requires_forward_output(
        ) { return layer<TAG_TYPE>(subnetwork).this_layer_requires_forward_output(); } 

        void disable_output_and_gradient_getters (
        ) { layer<TAG_TYPE>(subnetwork).disable_output_and_gradient_getters(); }

        tensor& private_get_output() const
        { return layer<TAG_TYPE>(subnetwork).private_get_output(); }
        tensor& private_get_gradient_input() 
        { return layer<TAG_TYPE>(subnetwork).private_get_gradient_input(); }

        subnet_type subnetwork;

        // This member doesn't logically contribute to the state of the object since it is
        // always empty. It's just here so we can have the get_parameter_gradient() methods
        // which have to return something.  So they return this empty tensor.
        resizable_tensor params_grad;
    };
    template <template<typename> class T, typename U>
    struct is_nonloss_layer_type<add_skip_layer<T,U>> : std::true_type {};

    template <typename SUBNET> using tag1  = add_tag_layer< 1, SUBNET>;
    template <typename SUBNET> using tag2  = add_tag_layer< 2, SUBNET>;
    template <typename SUBNET> using tag3  = add_tag_layer< 3, SUBNET>;
    template <typename SUBNET> using tag4  = add_tag_layer< 4, SUBNET>;
    template <typename SUBNET> using tag5  = add_tag_layer< 5, SUBNET>;
    template <typename SUBNET> using tag6  = add_tag_layer< 6, SUBNET>;
    template <typename SUBNET> using tag7  = add_tag_layer< 7, SUBNET>;
    template <typename SUBNET> using tag8  = add_tag_layer< 8, SUBNET>;
    template <typename SUBNET> using tag9  = add_tag_layer< 9, SUBNET>;
    template <typename SUBNET> using tag10 = add_tag_layer<10, SUBNET>;

    template <typename SUBNET> using skip1  = add_skip_layer< tag1, SUBNET>;
    template <typename SUBNET> using skip2  = add_skip_layer< tag2, SUBNET>;
    template <typename SUBNET> using skip3  = add_skip_layer< tag3, SUBNET>;
    template <typename SUBNET> using skip4  = add_skip_layer< tag4, SUBNET>;
    template <typename SUBNET> using skip5  = add_skip_layer< tag5, SUBNET>;
    template <typename SUBNET> using skip6  = add_skip_layer< tag6, SUBNET>;
    template <typename SUBNET> using skip7  = add_skip_layer< tag7, SUBNET>;
    template <typename SUBNET> using skip8  = add_skip_layer< tag8, SUBNET>;
    template <typename SUBNET> using skip9  = add_skip_layer< tag9, SUBNET>;
    template <typename SUBNET> using skip10 = add_skip_layer<tag10, SUBNET>;

// ----------------------------------------------------------------------------------------

    namespace timpl
    {
        inline void fill_with_gassuan_random_numbers (
            tensor& t,
            dlib::rand& rnd,
            double sigma = 1
        )
        {
            float* data = t.host();
            for (size_t i = 0; i < t.size(); ++i)
                data[i] = rnd.get_random_gaussian()*sigma;
        }

        class test_layer_subnet 
        {
        public:
            test_layer_subnet (
                dlib::rand& rnd_
            ) : rnd(rnd_) 
            {
                // Output and gradient_input have to have the same dimensions in each
                // layer.
                const long num_samples = rnd.get_random_32bit_number()%4+3;
                const long k  = rnd.get_random_32bit_number()%4+2;
                const long nr = rnd.get_random_32bit_number()%4+2;
                const long nc = rnd.get_random_32bit_number()%4+2;

                output.set_size(num_samples, k, nr, nc);
                gradient_input.set_size(num_samples, k, nr, nc);

                // Use a non-zero initial gradient to make sure the layers add to it
                // rather than assign and blow away the initial value.
                fill_with_gassuan_random_numbers(gradient_input, rnd, 0.01);

                fill_with_gassuan_random_numbers(output, rnd);
            }


            tensor& get_mutable_output() { return output; }
            const tensor& get_output() const { return output; }
            const tensor& private_get_output() const { return get_output(); }
            const test_layer_subnet& subnet() const { init_sub(); return *subnetwork; }

            tensor& get_gradient_input() { return gradient_input; }
            tensor& private_get_gradient_input() { return get_gradient_input(); }
            test_layer_subnet& subnet() { init_sub(); return *subnetwork; }



            unsigned long count_outputs() const
            {
                if (subnetwork)
                    return subnetwork->count_outputs() + output.size();
                else
                    return output.size();
            }

            float& get_output_element(unsigned long i)
            {
                if (i < output.size())
                    return output.host()[i];
                else
                    return subnet().get_output_element(i-output.size());
            }

            float get_gradient_input_element(unsigned long i) const
            {
                if (i < gradient_input.size())
                    return gradient_input.host()[i];
                else
                    return subnet().get_gradient_input_element(i-gradient_input.size());
            }


        private:
            // We lazily initialize sub-layers as needed when someone tries to call
            // subnet()
            void init_sub() const
            {
                if (!subnetwork)
                    subnetwork.reset(new test_layer_subnet(rnd));
            }

            dlib::rand& rnd;
            mutable std::unique_ptr<test_layer_subnet> subnetwork;
            resizable_tensor output;
            resizable_tensor gradient_input;
        };

    }

    struct layer_test_results
    {
        layer_test_results() : was_good(true) {}
        explicit layer_test_results(const std::string& l) : log(l),was_good(false) {}

        std::string log;
        bool was_good;

        operator bool() const { return was_good; }
    };

    inline std::ostream& operator<< (std::ostream& out, const layer_test_results& item)
    {
        out << item.log;
        return out;
    }

    template <
        typename layer_details_type
        >
    layer_test_results impl_test_layer (
        layer_details_type l,
        const float base_eps 
    )
    {
        using namespace timpl;
        // Do some setup
        running_stats<double> rs_data, rs_params;
        dlib::rand rnd;
        std::ostringstream sout;
        for (int iter = 0; iter < 10; ++iter)
        {
            test_layer_subnet subnetwork(rnd);
            resizable_tensor output, out2, out3;
            // Run setup() and forward() as well to make sure any calls to subnet() have
            // happened before we start assuming we know how many data elements there are
            // (since we do a lazy layer creation thing based on calls to subnet() inside
            // test_layer_subnet).
            l.setup(subnetwork);
            impl::call_layer_forward(l, subnetwork, output);

            resizable_tensor input_grad;
            input_grad.copy_size(output);
            fill_with_gassuan_random_numbers(input_grad, rnd);


            // The f() we are computing gradients of is this thing.  It's value at the current
            // parameter and data values is:
            //sout << "f(data,params): " << dot(output, input_grad) << std::endl;

            // We are going to save a copy of the subnetwork.get_gradient_input() data before we do
            // backpropagation since the backward() function is supposed to *add* to the
            // gradients rather than overwrite them.  We will use this saved data to check if
            // that is the case.
            const unsigned long num_data_inputs = subnetwork.count_outputs();
            std::vector<float> initial_gradient_input(num_data_inputs);
            for (unsigned long i = 0; i < num_data_inputs; ++i)
                initial_gradient_input[i] = subnetwork.get_gradient_input_element(i);


            // Now tell the layer to compute all the gradients.  In the rest of this function
            // we will just be checking that these gradients were computed correctly by
            // comparing them to a central differences approximation.
            resizable_tensor params_grad;
            params_grad.copy_size(l.get_layer_params());
            // But first, set the params grad to something crazy so that it's very obvious if
            // it doesn't get fully assigned.
            params_grad = std::numeric_limits<float>::infinity();
            impl::call_layer_backward(l, output, input_grad, subnetwork, params_grad);

            static_assert(impl::is_inplace_layer(l, subnetwork) == impl::has_inplace_backward(l, subnetwork),
                "Layer not defined correctly.  forward and backward methods must either both be in-place or both out-of-place. ");

            // Make sure the outputs of forward() and backward() are the same when they are run
            // in in-place mode.
            if (impl::is_inplace_layer(l, subnetwork))
            {
                test_layer_subnet subnetwork2(rnd);
                layer_details_type ll(l);
                ll.setup(subnetwork2);
                resizable_tensor ip_out;
                impl::call_layer_forward(ll, subnetwork2, ip_out);
                impl::call_layer_forward(ll, subnetwork2, subnetwork2.get_mutable_output());
                const auto forward_error = max(abs(mat(ip_out) - mat(subnetwork2.get_output())));
                if (forward_error > 0.00001)
                {
                    using namespace std;
                    sout << "This layer is supposed to support in-place computations but the output of forward_inplace()\n";
                    sout << "changes when invoked in-place vs. out-of-place. The error was: " << forward_error << endl;
                    return layer_test_results(sout.str()); 
                }

                resizable_tensor params_grad;
                params_grad.copy_size(ll.get_layer_params());
                params_grad = std::numeric_limits<float>::infinity();

                resizable_tensor input_grad;
                input_grad.copy_size(ip_out);
                fill_with_gassuan_random_numbers(input_grad, rnd);
                resizable_tensor params_grad1, params_grad2, data_grad1, data_grad2;
                params_grad1 = params_grad;
                params_grad2 = params_grad;
                // Now call backward() and make sure it works as well.  Recall that when an
                // in-place layer works in-place it assigns to it's outputs but when it's
                // not running in-place it adds.  So we initialize to a non-zero value to
                // check that this is the behavior that really executes.
                subnetwork2.get_gradient_input() = 9;
                impl::call_layer_backward(ll, ip_out, input_grad, subnetwork2, params_grad1);
                data_grad1 = subnetwork2.get_gradient_input();

                subnetwork2.get_gradient_input() = mat(input_grad);
                impl::call_layer_backward(ll, ip_out, subnetwork2.get_gradient_input(), subnetwork2, params_grad2);
                data_grad2 = subnetwork2.get_gradient_input();
                if (params_grad.size() != 0)
                {
                    const auto backward_param_error = max(abs(mat(params_grad1) - mat(params_grad2)));
                    if (backward_param_error > 0.00001)
                    {
                        using namespace std;
                        sout << "This layer is supposed to support in-place computations but the output of backward_inplace()\n";
                        sout << "changes when invoked in-place vs. out-of-place. The error was: " << backward_param_error << endl;
                        return layer_test_results(sout.str()); 
                    }
                }
                const auto backward_data_error = max(abs(mat(data_grad1)-9 - mat(data_grad2)));
                if (backward_data_error > 0.00001)
                {
                    using namespace std;
                    sout << "This layer is supposed to support in-place computations but the output of backward_inplace()\n";
                    sout << "changes when invoked in-place vs. out-of-place. The error was: " << backward_data_error << endl;
                    return layer_test_results(sout.str()); 
                }
            }

            // ==================================================================
            // first validate the way the parameter gradients are computed
            for (unsigned long i = 0; i < params_grad.size(); ++i)
            {
                layer_details_type l1(l);

                float eps = l1.get_layer_params().host()[i]*base_eps;
                if (eps == 0)
                    eps = base_eps;
                const float oldval = l1.get_layer_params().host()[i];
                l1.get_layer_params().host()[i] = oldval+eps;
                impl::call_layer_forward(l1, subnetwork, out2);
                l1.get_layer_params().host()[i] = oldval-eps;
                impl::call_layer_forward(l1, subnetwork, out3);
                l1.get_layer_params().host()[i] = oldval;

                // Compute a reference derivative via a central differences approximation and
                // compare it to the one output by the layer and make sure they match.
                double reference_derivative = (dot(out2,input_grad)-dot(out3, input_grad))/(2*eps);
                double output_derivative = params_grad.host()[i];
                double relative_error;
                if (reference_derivative != 0)
                    relative_error = (reference_derivative - output_derivative)/(reference_derivative);
                else
                    relative_error = (reference_derivative - output_derivative);
                double absolute_error = (reference_derivative - output_derivative);
                rs_params.add(std::abs(relative_error));
                if (std::abs(relative_error) > 0.05 && std::abs(absolute_error) > 0.006)
                {
                    using namespace std;
                    sout << "Gradient error in parameter #" << i <<".  Relative error: "<< relative_error << endl;
                    sout << "expected derivative: " << reference_derivative << endl;
                    sout << "output derivative:   " << output_derivative << endl;
                    sout << "iteration:           " << iter << endl;
                    return layer_test_results(sout.str()); 
                }
            }

            // ==================================================================
            // now validate the data gradients
            for (unsigned long i = 0; i < num_data_inputs; ++i)
            {
                const float oldval = subnetwork.get_output_element(i);
                float eps = oldval*base_eps;
                if (eps == 0)
                    eps = base_eps;
                subnetwork.get_output_element(i) = oldval+eps;
                impl::call_layer_forward(l, subnetwork, out2);
                subnetwork.get_output_element(i) = oldval-eps;
                impl::call_layer_forward(l, subnetwork, out3);
                subnetwork.get_output_element(i) = oldval;

                // Compute a reference derivative via a central differences approximation and
                // compare it to the one output by the layer and make sure they match.
                double reference_derivative = (dot(out2,input_grad)-dot(out3, input_grad))/(2*eps);
                double output_derivative = subnetwork.get_gradient_input_element(i);
                output_derivative -= initial_gradient_input[i];
                double relative_error;
                if (reference_derivative != 0)
                    relative_error = (reference_derivative - output_derivative)/(reference_derivative);
                else
                    relative_error = (reference_derivative - output_derivative);
                double absolute_error = (reference_derivative - output_derivative);
                rs_data.add(std::abs(relative_error));
                if (std::abs(relative_error) > 0.05 && std::abs(absolute_error) > 0.006)
                {
                    using namespace std;
                    sout << "Gradient error in data variable #" << i <<".  Relative error: "<< relative_error << endl;
                    sout << "expected derivative: " << reference_derivative << endl;
                    sout << "output derivative:   " << output_derivative << endl;
                    sout << "iteration:           " << iter << endl;
                    return layer_test_results(sout.str()); 
                }
            }

        } // end for (int iter = 0; iter < 5; ++iter)

        if (rs_params.mean() > 0.003)
        {
            using namespace std;
            sout << "Average parameter gradient error is somewhat large at: "<< rs_params.mean() << endl;
            return layer_test_results(sout.str()); 
        }
        if (rs_data.mean() > 0.003)
        {
            using namespace std;
            sout << "Average data gradient error is somewhat large at: "<< rs_data.mean() << endl;
            return layer_test_results(sout.str()); 
        }

        return layer_test_results();
    }

    template <
        typename layer_details_type
        >
    layer_test_results test_layer (
        layer_details_type l
    )
    {
        // Try a few different derivative step sizes to see if any work. 
        for (float base_eps = 0.0001; base_eps < 0.1; base_eps *= 2)
        {
            auto result = impl_test_layer(l, base_eps);
            if (result)
                return result;
        }
        // However, if none of the step sizes worked then try this one and probably result
        // in returning an error.
        return impl_test_layer(l, 0.01);
    }

// ----------------------------------------------------------------------------------------

    namespace impl
    {
        template <size_t i, size_t num>
        struct vlp_loop
        {
            template <typename T, typename U>
            static typename std::enable_if<!is_add_layer<U>::value>::type invoke_functor(T&& , size_t& , U&& )
            {
                // intentionally left empty
            }

            template <typename T, typename U>
            static typename std::enable_if<is_add_layer<U>::value>::type invoke_functor(T&& v , size_t& comp_i, U&& l )
            {
                v(comp_i, l.layer_details().get_layer_params());
                ++comp_i;
            }

            template <
                typename net_type,
                typename visitor
                >
            static void visit(
                size_t comp_i,
                net_type& net,
                visitor&& v
            )
            {
                invoke_functor(v, comp_i, layer<i>(net));
                vlp_loop<i+1, num>::visit(comp_i, net,v);
            }
        };

        template <size_t num>
        struct vlp_loop<num,num>
        {
            template <
                typename net_type,
                typename visitor
                >
            static void visit(
                size_t,
                net_type&,
                visitor&& 
            )
            {
                // Base case of recursion.  Don't do anything.
            }
        };

    }

    template <
        typename net_type,
        typename visitor
        >
    void visit_layer_parameters(
        net_type& net,
        visitor v
    )
    {
        size_t comp_i = 0;
        impl::vlp_loop<0, net_type::num_layers>::visit(comp_i, net, v);
    }

// ----------------------------------------------------------------------------------------

    namespace impl
    {
        template <size_t i, size_t num>
        struct vlpg_loop
        {
            template <typename T, typename U>
            static typename std::enable_if<!is_add_layer<U>::value>::type invoke_functor(T&& , size_t& , U&& )
            {
                // intentionally left empty
            }

            template <typename T, typename U>
            static typename std::enable_if<is_add_layer<U>::value>::type invoke_functor(T&& v , size_t& comp_i, U&& l )
            {
                v(comp_i, l.get_parameter_gradient());
                ++comp_i;
            }

            template <
                typename net_type,
                typename visitor
                >
            static void visit(
                size_t comp_i,
                net_type& net,
                visitor&& v
            )
            {
                invoke_functor(v, comp_i, layer<i>(net));
                vlpg_loop<i+1, num>::visit(comp_i, net,v);
            }
        };

        template <size_t num>
        struct vlpg_loop<num,num>
        {
            template <
                typename net_type,
                typename visitor
                >
            static void visit(
                size_t,
                net_type&,
                visitor&& 
            )
            {
                // Base case of recursion.  Don't do anything.
            }
        };

    }

    template <
        typename net_type,
        typename visitor
        >
    void visit_layer_parameter_gradients(
        net_type& net,
        visitor v
    )
    {
        size_t comp_i = 0;
        impl::vlpg_loop<0, net_type::num_layers>::visit(comp_i, net, v);
    }

// ----------------------------------------------------------------------------------------

    namespace impl
    {
        template <typename T>
        struct group_helper;
        template<typename... R>
        struct group_count_helper;
    }


    // --------------------------------------------------------------------------------------
    // this class is used to reference group layer input
    class group_input
    {
    public:
        typedef tensor input_type;
        const static unsigned int sample_expansion_factor = 1;
        friend void serialize(const group_input& item, std::ostream& out)
        {
            serialize("group_input", out);
        }

        friend void deserialize(group_input& item, std::istream& in)
        {
            std::string version;
            deserialize(version, in);
            if (version != "group_input")
                throw serialization_error("Unexpected version found while deserializing dlib::group_input.");
        }

        friend std::ostream& operator<<(std::ostream& out, const group_input& item)
        {
            out << "group_input";
            return out;
        }
    };
    // --------------------------------------------------------------------------------------

    template <typename GRP, typename SUBNET>
    class depth_group;


    template <typename T, typename U>
    struct is_nonloss_layer_type<depth_group<T,U>> : std::true_type {};

    template <typename GRP, typename SUBNET>
    class depth_group
    {
    public:
        typedef GRP grp_type;
        typedef SUBNET subnet_type;
        typedef typename subnet_type::input_type input_type;
        const static size_t group_size = std::tuple_size<grp_type>::value;
        const static size_t num_layers_in_group = impl::group_count_helper<GRP>::num_layers;
        const static size_t num_layers = subnet_type::num_layers + num_layers_in_group;
        const static size_t num_computational_layers_in_group = impl::group_count_helper<GRP>::num_computational_layers;
        const static size_t num_computational_layers = subnet_type::num_computational_layers + num_computational_layers_in_group;
        const static unsigned int sample_expansion_factor = subnet_type::sample_expansion_factor;

        using group_helper = impl::group_helper<grp_type>;

        depth_group(
        ):
                subnetwork(new subnet_type()),
                grp(new grp_type()),
                gradient_input_is_stale(true),
                get_output_and_gradient_input_disabled(false)
        {
        }

        depth_group(const depth_group& item)
        {
            grp.reset(new grp_type(*item.grp));
            subnetwork.reset(new subnet_type(*item.subnetwork));
            gradient_input_is_stale = item.gradient_input_is_stale;
            get_output_and_gradient_input_disabled = item.get_output_and_gradient_input_disabled;
            x_grad = item.x_grad;
            cached_output = item.cached_output;
            temp_tensor = item.temp_tensor;
        }
        depth_group& operator=(const depth_group& item) { depth_group(item).swap(*this); return *this;}
        depth_group(depth_group&& item) : depth_group() { swap(item); }
        depth_group& operator=(depth_group&& item) { swap(item); return *this; }

        template <typename T, typename U, typename E>
        friend class add_layer;
        template <typename T, bool is_first, typename E>
        friend class dimpl::subnet_wrapper;
        template <unsigned long T, typename U, typename E>
        friend class add_tag_layer;
        template <template<typename> class T, typename U>
        friend class add_skip_layer;
        template <size_t N, template<typename> class L, typename S>
        friend class repeat;

        // Allow copying networks from one to another as long as their corresponding
        // layers can be constructed from each other.
        template <typename T, typename U>
        depth_group(
                const depth_group<T,U>& item
        ) :
                grp(new grp_type(item.detail())),
                subnetwork(new subnet_type(item.subnet())),
                gradient_input_is_stale(item.gradient_input_is_stale),
                get_output_and_gradient_input_disabled(item.get_output_and_gradient_input_disabled),
                x_grad(item.x_grad),
                cached_output(item.cached_output)
        {
        }

        template <typename input_iterator>
        void to_tensor (
                input_iterator ibegin,
                input_iterator iend,
                resizable_tensor& data
        ) const
        {
            subnetwork->to_tensor(ibegin,iend,data);
        }

        template <typename input_iterator>
        const tensor& operator() (
                input_iterator ibegin,
                input_iterator iend
        )
        {
            to_tensor(ibegin,iend,temp_tensor);
            return forward(temp_tensor);
        }


        const tensor& operator() (const input_type& x)
        {
            return (*this)(&x, &x+1);
        }


        // forward for group: subnet->for_each_in_group->concat->cached_output
        const tensor& forward(const tensor& x)
        {

            subnetwork->forward(x);
            long group_depth = 0;

            group_helper::forward(subnetwork->get_output(), detail(), group_depth);

            auto& out_0 = std::get<0>(detail()).get_output();
            cached_output.set_size(out_0.num_samples(), group_depth, out_0.nr(), out_0.nc());

            group_helper::concat(cached_output, detail());


            gradient_input_is_stale = true;
            return private_get_output();
        }

    private:
        bool this_layer_requires_forward_output(
        )
        {
            return true;
        }

        tensor& private_get_output() const
        {
            return const_cast<resizable_tensor&>(cached_output);
        }
        tensor& private_get_gradient_input()
        {
            if (gradient_input_is_stale)
            {
                gradient_input_is_stale = false;
                x_grad.copy_size(private_get_output());
                x_grad = 0;
            }
            return x_grad;
        }
        void disable_output_and_gradient_getters (
        ) { get_output_and_gradient_input_disabled = true; }
    public:
        const tensor& get_output() const
        {
            if (get_output_and_gradient_input_disabled)
                throw dlib::error("Accessing this layer's get_output() is disabled because an in-place layer has been stacked on top of it.");
            return private_get_output();
        }
        tensor& get_gradient_input()
        {
            if (get_output_and_gradient_input_disabled)
                throw dlib::error("Accessing this layer's get_gradient_input() is disabled because an in-place layer has been stacked on top of it.");
            return private_get_gradient_input();
        }

        const tensor& get_final_data_gradient(
        ) const { return subnetwork->get_final_data_gradient(); }

        void back_propagate_error(const tensor& x)
        {
            back_propagate_error(x, private_get_gradient_input());
        }
        void back_propagate_error(const tensor& x, const tensor& gradient_input)
        {
            group_helper::backward(detail(), get_gradient_input(), subnetwork->get_output(), subnetwork->get_gradient_input());

            subnetwork->back_propagate_error(x);

            // zero out get_gradient_input()
            gradient_input_is_stale = true;
        }

        template <typename solver_type>
        void update_parameters(sstack<solver_type> solvers, double step_size)
        {
            DLIB_CASSERT(solvers.size()>=num_computational_layers,"");
            group_helper::update_parameters(solvers, step_size, detail());
            solvers = solvers.pop(num_computational_layers_in_group);
            subnetwork->update_parameters(solvers, step_size);
        }

        const subnet_type& subnet() const { return *subnetwork; }
        subnet_type& subnet() { return *subnetwork; }

        const grp_type& detail() const { return *grp; }
        grp_type& detail() { return *grp; }

        void clean()
        {
            x_grad.clear();
            cached_output.clear();
            temp_tensor.clear();
            gradient_input_is_stale = true;
            subnetwork->clean();
        }

        friend void serialize(const depth_group& item, std::ostream& out)
        {
            int version = 2;
            serialize(version, out);
            serialize(*item.subnetwork, out);
            group_helper::serialize(*item.grp, out);
            serialize(item.gradient_input_is_stale, out);
            serialize(item.get_output_and_gradient_input_disabled, out);
            serialize(item.x_grad, out);
            serialize(item.cached_output, out);
        }

        friend void deserialize(depth_group& item, std::istream& in)
        {
            int version = 0;
            deserialize(version, in);
            if (!(1 <= version && version <= 2))
                throw serialization_error("Unexpected version found while deserializing dlib::depth_group.");
            deserialize(*item.subnetwork, in);
            group_helper::deserialize(*item.grp, in);
            deserialize(item.gradient_input_is_stale, in);
            deserialize(item.get_output_and_gradient_input_disabled, in);
            deserialize(item.x_grad, in);
            deserialize(item.cached_output, in);
        }

        friend std::ostream& operator<< (std::ostream& out, const depth_group& item)
        {
            item.print(out, 0);
            return out;
        }

        void print (std::ostream& out, unsigned long idx=0) const
        {
            out << "layer<" << idx << ">\t";
            detail().print(out, idx);
            subnet().print(out, idx+1);
        }

    private:


        void swap(depth_group& item)
        {
            std::swap(subnetwork,item.subnetwork);
            std::swap(grp, item.grp);
            std::swap(gradient_input_is_stale, item.gradient_input_is_stale);
            std::swap(get_output_and_gradient_input_disabled, item.get_output_and_gradient_input_disabled);
            std::swap(x_grad, item.x_grad);
            std::swap(cached_output, item.cached_output);
        }


        std::unique_ptr<subnet_type> subnetwork;
        std::unique_ptr<grp_type> grp;

        bool gradient_input_is_stale;
        bool get_output_and_gradient_input_disabled;

        resizable_tensor x_grad;
        resizable_tensor cached_output;

        // temp_tensor doesn't logically contribute to the state of this object.
        // It is here only to prevent it from being reallocated over and over.
        resizable_tensor temp_tensor;
    };

    // define "grp" layer shorter name for usage when creating networks
    template <typename GRP, typename SUBNET>
    using grp = depth_group<GRP, SUBNET>;

    namespace impl {
        template<
                unsigned int i,
                typename T, typename U
        >
        struct layer_helper<i, depth_group<T, U>,
                typename std::enable_if<(i != 0 && i >= depth_group<T, U>::num_layers_in_group)>::type> {
            const static size_t num_layers_in_group = depth_group<T, U>::num_layers_in_group;

            using next_type = typename depth_group<T, U>::subnet_type;
            using type = typename layer_helper<i - num_layers_in_group, next_type>::type;

            static type &layer(depth_group<T, U> &n) {
                return layer_helper<i - num_layers_in_group, next_type>::layer(n.subnet());
            }
        };

        template<
                unsigned int i,
                typename T, typename U
        >
        struct layer_helper<i, depth_group<T, U>,
                typename std::enable_if<(i != 0 && i < depth_group<T, U>::num_layers_in_group)>::type> {
            const static size_t num_layers_in_group = depth_group<T, U>::num_layers_in_group;
            typedef typename depth_group<T, U>::grp_type grp_type;
            using type = typename layer_helper<i, grp_type>::type;

            static type &layer(depth_group<T, U> &n) {
                return layer_helper<i, grp_type>::layer(n.detail());
            }
        };

        template <unsigned int pos, unsigned int i, typename... T>
        struct group_pos_search{
            const static unsigned int count = sizeof...(T);
            const static unsigned int pos_from_begin = count - pos - 1;
            using tuple_elem_type = typename std::tuple_element<pos_from_begin, std::tuple<T...>>::type;
            static const unsigned int num_layers = tuple_elem_type::num_layers;

            static const unsigned int layer_index = i >= num_layers ? group_pos_search<pos - 1, i - num_layers, T...>::layer_index : i;
            static const unsigned int tuple_index = i >= num_layers ? group_pos_search<pos - 1, i - num_layers, T...>::tuple_index + 1 : pos;
        };
        template <unsigned int i, typename... T>
        struct group_pos_search<0, i, T...>{
            static const unsigned int layer_index = i;
            static const unsigned int tuple_index = 0;
        };


        template<
                unsigned int i,
                typename... R
        >
        struct layer_helper<i, std::tuple<R...>, typename std::enable_if<true>::type>{
            const static unsigned tuple_size = sizeof...(R);

            static const unsigned int layer_index = group_pos_search<tuple_size - 1, i, R...>::layer_index;
            static const unsigned int tuple_index = group_pos_search<tuple_size - 1, i, R...>::tuple_index;

            using next_type = typename std::tuple_element<tuple_index, std::tuple<R...>>::type;//typename std::remove_reference<decltype(makeT().subnet())>::type;
            using type = typename layer_helper<layer_index,next_type>::type;

            static type &layer(std::tuple<R...> &n) {
                return layer_helper<layer_index, next_type>::layer(std::get<tuple_index>(n));
            }
        };

        // helper classes for layer group processing
        template <size_t idx, typename... T>
        struct group_helper_impl{
            static void serialize_impl(const std::tuple<T...>& data, std::ostream& out){
                group_helper_impl<idx - 1, T...>::serialize_impl(data, out);
                serialize(std::get<idx>(data), out);
            }
            static void deserialize_impl(std::tuple<T...>& data, std::istream& in){
                group_helper_impl<idx - 1, T...>::deserialize_impl(data, in);
                deserialize(std::get<idx>(data), in);
            }
            static void forward(const tensor& x, std::tuple<T...>& grp, long& group_depth){
                group_helper_impl<idx - 1, T...>::forward(x, grp, group_depth);
                auto& r = std::get<idx>(grp).forward(x);
                group_depth += r.k();
            }
            static size_t concat(resizable_tensor& cached_output, std::tuple<T...>& grp, size_t offset){
                offset += group_helper_impl<idx - 1, T...>::concat(cached_output, grp, offset);
                auto& output = std::get<idx>(grp).get_output();
                tt::concat_depth(cached_output, offset, output);
                return offset + output.nc() * output.nr() * output.k();
            }
            template<typename solver_type>
            static sstack<solver_type> update_parameters(sstack<solver_type> solvers, double step_size, std::tuple<T...>& grp){
                sstack<solver_type> sub_solvers = group_helper_impl<idx - 1, T...>::update_parameters(solvers, step_size, grp);
                std::get<idx>(grp).update_parameters(sub_solvers, step_size);
                using tuple_elem_type = typename std::tuple_element<idx, std::tuple<T...>>::type;
                return sub_solvers.pop(tuple_elem_type::num_computational_layers);
            }
            static size_t backward(std::tuple<T...>& grp, const tensor& group_gradient_in,
                                         const tensor& subnet_out, tensor& group_gradient_out, size_t offset)
            {
                offset += group_helper_impl<idx - 1, T...>::backward(grp, group_gradient_in, subnet_out, group_gradient_out, offset);

                auto& subnet = std::get<idx>(grp);
                auto& gr_input = subnet.get_gradient_input();
                tt::split_depth(gr_input, offset, group_gradient_in);

                subnet.back_propagate_error(subnet_out);

                tt::add(group_gradient_out, group_gradient_out, subnet.get_final_data_gradient());
                return offset + gr_input.nc() * gr_input.nr() * gr_input.k();
            }
        };
        template <typename... T>
        struct group_helper_impl<0, T...>{
            static void serialize_impl(const std::tuple<T...>& data, std::ostream& out){
                serialize(std::get<0>(data), out);
            }
            static void deserialize_impl(std::tuple<T...>& data, std::istream& in){
                deserialize(std::get<0>(data), in);
            }
            static void forward(const tensor& x, std::tuple<T...>& grp, long& group_depth){
                auto& r = std::get<0>(grp).forward(x);
                group_depth += r.k();
            }
            static size_t concat(resizable_tensor& cached_output, std::tuple<T...>& grp, size_t offset){
                auto& output = std::get<0>(grp).get_output();
                tt::concat_depth(cached_output, offset, output);
                return offset + output.nc() * output.nr() * output.k();
            }
            template<typename solver_type>
            static sstack<solver_type> update_parameters(sstack<solver_type> solvers, double step_size, std::tuple<T...>& grp){
                std::get<0>(grp).update_parameters(solvers, step_size);
                using tuple_elem_type = typename std::tuple_element<0, std::tuple<T...>>::type;
                return solvers.pop(tuple_elem_type::num_computational_layers);
            }
            static size_t backward(std::tuple<T...>& grp, const tensor& group_gradient_in,
                                         const tensor& subnet_out, tensor& group_gradient_out, size_t offset)
            {
                auto& item = std::get<0>(grp);
                auto& gr_input = item.get_gradient_input();
                tt::split_depth(gr_input, offset, group_gradient_in);
                item.back_propagate_error(subnet_out);

                tt::add(group_gradient_out, group_gradient_out, item.get_final_data_gradient());
                return offset + gr_input.nc() * gr_input.nr() * gr_input.k();
            }
        };
        template <typename... T>
        struct group_helper<std::tuple<T...>>{
            static void serialize(const std::tuple<T...> & data, std::ostream& out){
                group_helper_impl<std::tuple_size<std::tuple<T...>>::value - 1, T...>::serialize_impl(data, out);
            }
            static void deserialize(std::tuple<T...>& data, std::istream& in){
                group_helper_impl<std::tuple_size<std::tuple<T...>>::value - 1, T...>::deserialize_impl(data, in);
            }
            static void forward(const tensor& x, std::tuple<T...>& grp, long& group_depth){
                group_helper_impl<std::tuple_size<std::tuple<T...>>::value - 1, T...>::forward(x, grp, group_depth);
            }
            static void concat(resizable_tensor& out, std::tuple<T...>& grp){
                group_helper_impl<std::tuple_size<std::tuple<T...>>::value - 1, T...>::concat(out, grp, 0);
            }
            template<typename solver_type>
            static void update_parameters(sstack<solver_type> solvers, double step_size, std::tuple<T...>& grp){
                group_helper_impl<std::tuple_size<std::tuple<T...>>::value - 1, T...>::update_parameters(solvers, step_size, grp);
            }
            static void backward(std::tuple<T...>& grp, const tensor& group_gradient_in, const tensor& subnet_out, tensor& group_gradient_out)
            {
                group_helper_impl<std::tuple_size<std::tuple<T...>>::value - 1, T...>::backward(grp, group_gradient_in, subnet_out, group_gradient_out, 0);
            }
        };

        // helper classes to understand the count of group items layers
        template<typename T>
        struct group_count_helper<T>{
            const static size_t num_layers = T::num_layers;
            const static size_t num_computational_layers = T::num_computational_layers;
        };

        template<typename T, typename... R>
        struct group_count_helper<T, R...>{
            const static size_t num_layers = group_count_helper<T>::num_layers + group_count_helper<R...>::num_layers;
            const static size_t num_computational_layers = group_count_helper<T>::num_computational_layers + group_count_helper<R...>::num_computational_layers;
        };
        template<typename... R>
        struct group_count_helper<std::tuple<R...>>{
            const static size_t num_layers = group_count_helper<R...>::num_layers;
            const static size_t num_computational_layers = group_count_helper<R...>::num_computational_layers;
        };

    }
}

#endif // DLIB_DNn_CORE_H_


