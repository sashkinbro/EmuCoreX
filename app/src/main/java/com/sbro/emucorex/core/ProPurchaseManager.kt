package com.sbro.emucorex.core

import android.app.Activity
import android.content.Context
import android.util.Log
import androidx.annotation.StringRes
import com.android.billingclient.api.AcknowledgePurchaseParams
import com.android.billingclient.api.BillingClient
import com.android.billingclient.api.BillingClientStateListener
import com.android.billingclient.api.BillingFlowParams
import com.android.billingclient.api.BillingResult
import com.android.billingclient.api.PendingPurchasesParams
import com.android.billingclient.api.ProductDetails
import com.android.billingclient.api.Purchase
import com.android.billingclient.api.PurchasesUpdatedListener
import com.android.billingclient.api.QueryProductDetailsParams
import com.android.billingclient.api.QueryPurchasesParams
import com.sbro.emucorex.BuildConfig
import com.sbro.emucorex.R
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.ui.theme.ThemeMode
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.launch

private const val TAG = "ProPurchaseManager"

enum class ProPurchaseTier(val productId: String) {
    BASE("emucorex_pro"),
    SUPPORTER("emucorex_pro_supporter"),
    PATRON("emucorex_pro_patron");

    companion object {
        val productIds: Set<String> = entries.mapTo(linkedSetOf(), ProPurchaseTier::productId)

        fun fromProductId(productId: String): ProPurchaseTier? {
            return entries.firstOrNull { it.productId == productId }
        }
    }
}

data class ProProductOffer(
    val tier: ProPurchaseTier,
    val title: String,
    val description: String,
    val formattedPrice: String
)

fun availableProSupportOffers(
    offers: List<ProProductOffer>,
    ownedProductIds: Set<String>
): List<ProProductOffer> {
    val ownsSupportTier = ProPurchaseTier.SUPPORTER.productId in ownedProductIds ||
        ProPurchaseTier.PATRON.productId in ownedProductIds
    if (ownsSupportTier) return emptyList()
    return offers.filter { offer ->
        offer.tier != ProPurchaseTier.BASE && offer.tier.productId !in ownedProductIds
    }
}

fun canPurchaseProTier(
    tier: ProPurchaseTier,
    isProUnlocked: Boolean,
    ownedProductIds: Set<String>
): Boolean {
    if (tier.productId in ownedProductIds) return false
    val ownsSupportTier = ProPurchaseTier.SUPPORTER.productId in ownedProductIds ||
        ProPurchaseTier.PATRON.productId in ownedProductIds
    if (ownsSupportTier) return false
    return tier != ProPurchaseTier.BASE || !isProUnlocked
}

data class ProPurchaseState(
    val isProUnlocked: Boolean = false,
    val isPurchaseStatusVerified: Boolean = false,
    val isBillingReady: Boolean = false,
    val isPurchaseInProgress: Boolean = false,
    val isProductLoading: Boolean = false,
    val isProductAvailable: Boolean = false,
    val productTitle: String? = null,
    val productPrice: String? = null,
    val products: List<ProProductOffer> = emptyList(),
    val ownedProductIds: Set<String> = emptySet(),
    val purchaseProductId: String? = null,
    @param:StringRes val messageResId: Int? = null
)

class ProPurchaseManager private constructor(context: Context) : PurchasesUpdatedListener {

    private val appContext = context.applicationContext
    private val preferences = AppPreferences(appContext)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var productDetailsById: Map<String, ProductDetails> = emptyMap()

    private val billingClient = BillingClient.newBuilder(appContext)
        .setListener(this)
        .enablePendingPurchases(
            PendingPurchasesParams.newBuilder()
                .enableOneTimeProducts()
                .build()
        )
        .enableAutoServiceReconnection()
        .build()

    private val _state = MutableStateFlow(ProPurchaseState())
    val state: StateFlow<ProPurchaseState> = _state.asStateFlow()

    init {
        scope.launch {
            preferences.proUnlocked.distinctUntilChanged().collect { unlocked ->
                _state.value = _state.value.copy(isProUnlocked = unlocked)
            }
        }
        connect()
    }

    fun connect() {
        if (billingClient.isReady) {
            _state.value = _state.value.copy(isBillingReady = true)
            queryProductDetails(showMessage = false)
            restorePurchases(showMessage = false)
            return
        }

        billingClient.startConnection(object : BillingClientStateListener {
            override fun onBillingSetupFinished(billingResult: BillingResult) {
                val ready = billingResult.responseCode == BillingClient.BillingResponseCode.OK
                _state.value = _state.value.copy(
                    isBillingReady = ready,
                    messageResId = if (ready) null else R.string.pro_message_unavailable
                )
                if (ready) {
                    queryProductDetails(showMessage = false)
                    restorePurchases(showMessage = false)
                }
            }

            override fun onBillingServiceDisconnected() {
                _state.value = _state.value.copy(isBillingReady = false)
            }
        })
    }

    fun purchase(
        activity: Activity,
        tier: ProPurchaseTier = ProPurchaseTier.BASE
    ) {
        val currentState = _state.value
        if (currentState.isPurchaseInProgress) return
        if (
            !canPurchaseProTier(
                tier = tier,
                isProUnlocked = currentState.isProUnlocked,
                ownedProductIds = currentState.ownedProductIds
            )
        ) {
            _state.value = _state.value.copy(messageResId = R.string.pro_message_already_active)
            return
        }
        if (!billingClient.isReady) {
            connect()
            _state.value = _state.value.copy(messageResId = R.string.pro_message_unavailable)
            return
        }

        val details = productDetailsById[tier.productId]
        if (details == null) {
            queryProductDetails(showMessage = true)
            return
        }

        val productParamsBuilder = BillingFlowParams.ProductDetailsParams.newBuilder()
            .setProductDetails(details)
        details.oneTimePurchaseOfferDetailsList?.firstOrNull()?.offerToken?.takeIf { it.isNotBlank() }?.let {
            productParamsBuilder.setOfferToken(it)
        }

        val flowParams = BillingFlowParams.newBuilder()
            .setProductDetailsParamsList(listOf(productParamsBuilder.build()))
            .build()

        _state.value = _state.value.copy(
            isPurchaseInProgress = true,
            purchaseProductId = tier.productId,
            messageResId = null
        )
        val result = billingClient.launchBillingFlow(activity, flowParams)
        if (result.responseCode != BillingClient.BillingResponseCode.OK) {
            _state.value = _state.value.copy(
                isPurchaseInProgress = false,
                purchaseProductId = null,
                messageResId = R.string.pro_message_purchase_open_failed
            )
        }
    }

    fun restorePurchases(showMessage: Boolean = true) {
        if (!billingClient.isReady) {
            connect()
            if (showMessage) {
                _state.value = _state.value.copy(messageResId = R.string.pro_message_unavailable)
            }
            return
        }

        val params = QueryPurchasesParams.newBuilder()
            .setProductType(BillingClient.ProductType.INAPP)
            .build()
        billingClient.queryPurchasesAsync(params) { billingResult, purchases ->
            if (billingResult.responseCode == BillingClient.BillingResponseCode.OK) {
                if (BuildConfig.DEBUG) {
                    Log.i(
                        TAG,
                        "Purchase restore returned=" +
                            purchases.joinToString(prefix = "[", postfix = "]") { purchase ->
                                "${purchase.products}:${purchase.purchaseState}"
                            }
                    )
                }
                val activePurchases = purchases.filter(::isActiveProPurchase)
                val ownedProductIds = activePurchases
                    .flatMap { it.products }
                    .filterTo(linkedSetOf(), ProPurchaseTier.productIds::contains)
                val hasPro = activePurchases.isNotEmpty()
                _state.value = _state.value.copy(
                    isPurchaseStatusVerified = true,
                    ownedProductIds = ownedProductIds
                )
                if (hasPro) {
                    handlePurchases(activePurchases, showUnlockMessage = showMessage)
                } else {
                    lockPro()
                }
                if (showMessage && !hasPro) {
                    _state.value = _state.value.copy(messageResId = R.string.pro_message_restore_missing)
                }
            } else if (showMessage) {
                _state.value = _state.value.copy(messageResId = R.string.pro_message_restore_failed)
            }
        }
    }

    fun clearMessage() {
        _state.value = _state.value.copy(messageResId = null)
    }

    override fun onPurchasesUpdated(billingResult: BillingResult, purchases: MutableList<Purchase>?) {
        when (billingResult.responseCode) {
            BillingClient.BillingResponseCode.OK -> {
                if (!purchases.isNullOrEmpty()) {
                    handlePurchases(purchases, showUnlockMessage = true)
                }
            }
            BillingClient.BillingResponseCode.USER_CANCELED -> {
                _state.value = _state.value.copy(
                    isPurchaseInProgress = false,
                    purchaseProductId = null
                )
            }
            else -> {
                _state.value = _state.value.copy(
                    isPurchaseInProgress = false,
                    purchaseProductId = null,
                    messageResId = R.string.pro_message_purchase_failed
                )
            }
        }
    }

    private fun queryProductDetails(showMessage: Boolean) {
        productDetailsById = emptyMap()
        _state.value = _state.value.copy(
            isProductLoading = true,
            isProductAvailable = false,
            productTitle = null,
            productPrice = null,
            products = emptyList(),
            messageResId = null
        )

        val products = ProPurchaseTier.entries.map { tier ->
            QueryProductDetailsParams.Product.newBuilder()
                .setProductId(tier.productId)
                .setProductType(BillingClient.ProductType.INAPP)
                .build()
        }
        val params = QueryProductDetailsParams.newBuilder()
            .setProductList(products)
            .build()

        billingClient.queryProductDetailsAsync(params) { billingResult, result ->
            if (billingResult.responseCode != BillingClient.BillingResponseCode.OK) {
                Log.w(TAG, "Product details query failed: code=${billingResult.responseCode}, message=${billingResult.debugMessage}")
                _state.value = _state.value.copy(
                    isProductLoading = false,
                    isProductAvailable = false,
                    messageResId = if (showMessage) R.string.pro_message_unavailable else null
                )
                return@queryProductDetailsAsync
            }

            val returnedDetails = result.productDetailsList
                .filter { it.productId in ProPurchaseTier.productIds }
                .associateBy(ProductDetails::getProductId)
            if (BuildConfig.DEBUG) {
                Log.i(
                    TAG,
                    "Product query fetched=${returnedDetails.keys}; " +
                        "unfetched=${result.unfetchedProductList.joinToString(prefix = "[", postfix = "]") {
                            "${it.productId}:${it.statusCode}"
                        }}"
                )
            }
            val offers = ProPurchaseTier.entries.mapNotNull { tier ->
                val details = returnedDetails[tier.productId] ?: return@mapNotNull null
                val price = details.oneTimePurchaseOfferDetailsList
                    ?.firstOrNull()
                    ?.formattedPrice
                    ?: details.oneTimePurchaseOfferDetails?.formattedPrice
                    ?: return@mapNotNull null
                ProProductOffer(
                    tier = tier,
                    title = details.title,
                    description = details.description,
                    formattedPrice = price
                )
            }
            if (BuildConfig.DEBUG) {
                Log.i(
                    TAG,
                    "Eligible Pro offers=${offers.joinToString { "${it.tier.productId}:${it.formattedPrice}" }}"
                )
            }
            val baseOffer = offers.firstOrNull { it.tier == ProPurchaseTier.BASE }
            if (baseOffer == null) {
                Log.w(
                    TAG,
                    "Base Pro product details missing. Returned products=${result.productDetailsList.map { it.productId }}"
                )
                productDetailsById = returnedDetails
                _state.value = _state.value.copy(
                    isProductLoading = false,
                    isProductAvailable = false,
                    productTitle = null,
                    productPrice = null,
                    products = offers,
                    messageResId = if (showMessage) R.string.pro_message_unavailable else null
                )
                return@queryProductDetailsAsync
            }

            productDetailsById = returnedDetails
            _state.value = _state.value.copy(
                isProductLoading = false,
                isProductAvailable = true,
                productTitle = baseOffer.title,
                productPrice = baseOffer.formattedPrice,
                products = offers,
                messageResId = null
            )
        }
    }

    private fun handlePurchases(purchases: List<Purchase>, showUnlockMessage: Boolean) {
        val activePurchases = purchases.filter(::isActiveProPurchase)
        val ownedProductIds = activePurchases
            .flatMap { it.products }
            .filterTo(linkedSetOf(), ProPurchaseTier.productIds::contains)
        _state.value = _state.value.copy(ownedProductIds = ownedProductIds)

        activePurchases.forEach { purchase ->
            if (purchase.isAcknowledged) {
                unlockPro(showUnlockMessage)
            } else {
                val params = AcknowledgePurchaseParams.newBuilder()
                    .setPurchaseToken(purchase.purchaseToken)
                    .build()
                billingClient.acknowledgePurchase(params) { result ->
                    if (result.responseCode == BillingClient.BillingResponseCode.OK) {
                        unlockPro(showUnlockMessage)
                    } else {
                        _state.value = _state.value.copy(
                            isPurchaseInProgress = false,
                            purchaseProductId = null,
                            messageResId = R.string.pro_message_pending_confirmation
                        )
                    }
                }
            }
        }
    }

    private fun unlockPro(showMessage: Boolean) {
        scope.launch {
            val wasUnlocked = preferences.proUnlocked.first()
            preferences.setProUnlocked(true)
            if (!wasUnlocked) {
                preferences.setThemeMode(ThemeMode.PRO)
            }
            _state.value = _state.value.copy(
                isProUnlocked = true,
                isPurchaseInProgress = false,
                purchaseProductId = null,
                messageResId = if (showMessage) R.string.pro_message_active else null
            )
        }
    }

    private fun lockPro() {
        scope.launch {
            preferences.setProUnlocked(false)
            _state.value = _state.value.copy(
                isProUnlocked = false,
                isPurchaseInProgress = false,
                purchaseProductId = null,
                ownedProductIds = emptySet()
            )
        }
    }

    private fun isActiveProPurchase(purchase: Purchase): Boolean {
        return purchase.purchaseState == Purchase.PurchaseState.PURCHASED &&
            purchase.products.any(ProPurchaseTier.productIds::contains)
    }

    companion object {
        @Volatile
        private var instance: ProPurchaseManager? = null

        fun getInstance(context: Context): ProPurchaseManager {
            return instance ?: synchronized(this) {
                instance ?: ProPurchaseManager(context).also { instance = it }
            }
        }
    }
}
