
const tradeUrl=window.location.href


const modal=document.getElementById("modal-content")


// open and close modal
const openModalBtn=document.getElementById("open-modal-btn")
openModalBtn.addEventListener('click', ()=>{
    modal.classList.remove('hidden')

})

const cancelModalBtn=document.getElementById("cancel-modal-btn")
cancelModalBtn.addEventListener('click' ,()=>{
    modal.classList.add('hidden')
})

//reference to form where we add new trade to db.
const newTradeForm=document.getElementById("new-trade-form")

//we need also grab all possible fields in this form.

//csrf token
const csrf=document.getElementsByName('csrfmiddlewaretoken')

$.ajax({
    type:"GET",
    url:"trade-book",
    success: function(response){
        console.log(response)

        const data=response.trades
        // add headers
        const headers=
        `<thead>`+      
                +`<tr>`
                    +`<th scope="col">Trade ID</th>`
                    +`<th scope="col">Underlier Ticker</th>`
                    +`<th scope="col">Product Type</th>`
                    +`<th scope="col">Payoff</th>`
                    +`<th scope="col">Trade Date</th>`
                    +`<th scope="col">Trade Maturity</th>`
                    +`<th scope="col">Strike</th>`
                    +`<th scope="col">Dividend</th>`
                    +`<th scope="col">user_id</th>`

                +`<tr>`+
        `<thead>`
        $('#trade-book-table').append(headers)  
        // add table body     
        data.forEach(el => {
        new_row=`<tr>`
        +`<td>`+`<a href="${tradeUrl}/${el.pk}">${el.pk}</a>`+`</td>`
        +`<td>`+el.underlying_ticker+`</td>`
        +`<td>`+el.product_type+`</td>`
        +`<td>`+el.payoff+`</td>`
        +`<td>`+el.trade_date+`</td>`
        +`<td>`+el.trade_maturity+`</td>`
        +`<td>`+el.strike+`</td>`
        +`<td>`+el.dividend+`</td>`
        +`<td>`+el.user_id+`</td>`

        +`<tr>`
        $('#trade-book-table').append(new_row) 
            
        });
        
        console.log(data)

    },
    error:function(error){
        console.log(error)
    }
})


$("#add-trade-btn").click(function(e){
    e.preventDefault()
    const underlying_ticker=document.getElementById("id_underlying_ticker")
    const product_type=document.getElementById("id_product_type")
    const payoff=document.getElementById("id_payoff")

    const trade_date=document.getElementById("id_trade_date")
    const trade_maturity=document.getElementById("id_trade_maturity")
    const strike=document.getElementById("id_strike")
    const dividend=document.getElementById("id_dividend")
    const user_id=document.getElementById("id_user_id")

    $.ajax({
        type: 'POST',
        url: '',
        data:{
            'csrfmiddlewaretoken':csrf[0].value,
            "underlying_ticker":underlying_ticker.value,
            "product_type":product_type.value,
            "payoff":payoff.value,
            "trade_date":trade_date.value,
            "trade_maturity":trade_maturity.value,
            "strike":strike.value,
            "dividend":dividend.value,
            "user_id":user_id.value
        },
        dataType:'json',
        success:function (data){
            console.log(data)
        },
        error:function(error){
            console.log(error)
        }
    })
})


