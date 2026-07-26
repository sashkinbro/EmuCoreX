package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class ProPurchaseTierTest {

    @Test
    fun productIds_matchPlayConsoleConfiguration() {
        assertEquals("emucorex_pro", ProPurchaseTier.BASE.productId)
        assertEquals("emucorex_pro_supporter", ProPurchaseTier.SUPPORTER.productId)
        assertEquals("emucorex_pro_patron", ProPurchaseTier.PATRON.productId)
        assertEquals(3, ProPurchaseTier.productIds.size)
    }

    @Test
    fun fromProductId_recognizesEveryProTier() {
        ProPurchaseTier.entries.forEach { tier ->
            assertEquals(tier, ProPurchaseTier.fromProductId(tier.productId))
        }
        assertNull(ProPurchaseTier.fromProductId("emucorex_pro_supporte"))
        assertNull(ProPurchaseTier.fromProductId("unrelated_product"))
    }

    @Test
    fun supportOffers_areAvailableToBaseProOwners() {
        assertEquals(
            listOf(ProPurchaseTier.SUPPORTER, ProPurchaseTier.PATRON),
            availableProSupportOffers(
                offers = allOffers(),
                ownedProductIds = setOf(ProPurchaseTier.BASE.productId)
            ).map(ProProductOffer::tier)
        )
    }

    @Test
    fun supporterOwners_doNotSeeAnotherPaidTier() {
        assertEquals(
            emptyList<ProProductOffer>(),
            availableProSupportOffers(
                offers = allOffers(),
                ownedProductIds = setOf(ProPurchaseTier.SUPPORTER.productId)
            )
        )
    }

    @Test
    fun patronOwners_doNotSeeLowerSupportTiers() {
        assertEquals(
            emptyList<ProProductOffer>(),
            availableProSupportOffers(
                offers = allOffers(),
                ownedProductIds = setOf(ProPurchaseTier.PATRON.productId)
            )
        )
    }

    @Test
    fun baseProOwners_canPurchaseEitherSupportTierButNotBaseAgain() {
        val owned = setOf(ProPurchaseTier.BASE.productId)

        assertEquals(false, canPurchaseProTier(ProPurchaseTier.BASE, true, owned))
        assertEquals(true, canPurchaseProTier(ProPurchaseTier.SUPPORTER, true, owned))
        assertEquals(true, canPurchaseProTier(ProPurchaseTier.PATRON, true, owned))
    }

    @Test
    fun supporterOwners_cannotPurchaseAnotherTier() {
        val owned = setOf(ProPurchaseTier.SUPPORTER.productId)

        ProPurchaseTier.entries.forEach { tier ->
            assertEquals(false, canPurchaseProTier(tier, true, owned))
        }
    }

    @Test
    fun patronOwners_cannotPurchaseAnyTierAgain() {
        val owned = setOf(ProPurchaseTier.PATRON.productId)

        ProPurchaseTier.entries.forEach { tier ->
            assertEquals(false, canPurchaseProTier(tier, true, owned))
        }
    }

    private fun allOffers(): List<ProProductOffer> {
        return ProPurchaseTier.entries.map { tier ->
            ProProductOffer(
                tier = tier,
                title = tier.name,
                description = tier.name,
                formattedPrice = tier.name
            )
        }
    }
}
