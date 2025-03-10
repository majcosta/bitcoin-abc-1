import React, { useState, useEffect } from 'react';
import { useLocation } from 'react-router-dom';
import PropTypes from 'prop-types';
import { WalletContext } from 'utils/context';
import {
    AntdFormWrapper,
    SendBchInput,
    DestinationAddressSingle,
    DestinationAddressMulti,
} from 'components/Common/EnhancedInputs';
import { ThemedMailOutlined } from 'components/Common/CustomIcons';
import { CustomCollapseCtn } from 'components/Common/StyledCollapse';
import { Form, message, Modal, Alert, Input } from 'antd';
import { Row, Col, Switch } from 'antd';
import PrimaryButton, { DisabledButton } from 'components/Common/PrimaryButton';
import useWindowDimensions from 'hooks/useWindowDimensions';
import {
    sendXecNotification,
    errorNotification,
} from 'components/Common/Notifications';
import { isMobile, isIOS, isSafari } from 'react-device-detect';
import { currency, parseAddressForParams } from 'components/Common/Ticker.js';
import { Event } from 'utils/GoogleAnalytics';
import {
    fiatToCrypto,
    shouldRejectAmountInput,
    isValidXecAddress,
    isValidEtokenAddress,
    isValidXecSendAmount,
} from 'utils/validation';
import BalanceHeader from 'components/Common/BalanceHeader';
import BalanceHeaderFiat from 'components/Common/BalanceHeaderFiat';
import {
    ZeroBalanceHeader,
    ConvertAmount,
    AlertMsg,
    WalletInfoCtn,
    SidePaddingCtn,
    FormLabel,
} from 'components/Common/Atoms';
import { getWalletState, fromSatoshisToXec, calcFee } from 'utils/cashMethods';
import { sendXec } from 'utils/transactions';
import ApiError from 'components/Common/ApiError';
import { formatFiatBalance, formatBalance } from 'utils/formatting';
import styled from 'styled-components';
import WalletLabel from 'components/Common/WalletLabel.js';

const { TextArea } = Input;

const TextAreaLabel = styled.div`
    text-align: left;
    color: ${props => props.theme.forms.text};
    padding-left: 1px;
    white-space: nowrap;
`;

const AmountPreviewCtn = styled.div`
    display: flex;
    flex-direction: column;
    justify-content: top;
    max-height: 1rem;
`;

const SendInputCtn = styled.div`
    .ant-form-item-explain-error {
        @media (max-width: 300px) {
            font-size: 12px;
        }
    }
`;

const LocaleFormattedValue = styled.h3`
    color: ${props => props.theme.contrast};
    font-weight: bold;
    margin-bottom: 0;
`;

const SendAddressHeader = styled.div`
    display: flex;
    align-items: center;
`;
const DestinationAddressSingleCtn = styled.div``;
const DestinationAddressMultiCtn = styled.div``;

const ExpandingAddressInputCtn = styled.div`
    min-height: 14rem;
    ${DestinationAddressSingleCtn} {
        overflow: hidden;
        transition: ${props =>
            props.open
                ? 'max-height 200ms ease-in, opacity 200ms ease-out'
                : 'max-height 200ms cubic-bezier(0, 1, 0, 1), opacity 200ms ease-in'};
        max-height: ${props => (props.open ? '0rem' : '12rem')};
        opacity: ${props => (props.open ? 0 : 1)};
    }
    ${DestinationAddressMultiCtn} {
        overflow: hidden;
        transition: ${props =>
            props.open
                ? 'max-height 200ms ease-in, transform 200ms ease-out, opacity 200ms ease-in'
                : 'max-height 200ms cubic-bezier(0, 1, 0, 1), transform 200ms ease-out'};
        max-height: ${props => (props.open ? '13rem' : '0rem')};
        transform: ${props =>
            props.open ? 'translateY(0%)' : 'translateY(100%)'};
        opacity: ${props => (props.open ? 1 : 0)};
    }
`;

const PanelHeaderCtn = styled.div`
    display: flex;
    justify-content: center;
    align-items: center;
    gap: 1rem;
`;

const SendBCH = ({ passLoadingStatus }) => {
    // use balance parameters from wallet.state object and not legacy balances parameter from walletState, if user has migrated wallet
    // this handles edge case of user with old wallet who has not opened latest Cashtab version yet

    // If the wallet object from ContextValue has a `state key`, then check which keys are in the wallet object
    // Else set it as blank
    const ContextValue = React.useContext(WalletContext);
    const location = useLocation();
    const {
        wallet,
        fiatPrice,
        apiError,
        cashtabSettings,
        changeCashtabSettings,
        chronik,
    } = ContextValue;
    const walletState = getWalletState(wallet);
    const { balances, nonSlpUtxos } = walletState;
    // Modal settings
    const [isOneToManyXECSend, setIsOneToManyXECSend] = useState(false);
    const [opReturnMsg, setOpReturnMsg] = useState(false);
    const [isEncryptedOptionalOpReturnMsg, setIsEncryptedOptionalOpReturnMsg] =
        useState(false);

    // Get device window width
    // If this is less than 769, the page will open with QR scanner open
    const { width } = useWindowDimensions();
    // Load with QR code open if device is mobile and NOT iOS + anything but safari
    const scannerSupported =
        cashtabSettings &&
        cashtabSettings.autoCameraOn === true &&
        width < 769 &&
        isMobile &&
        !(isIOS && !isSafari);

    const [formData, setFormData] = useState({
        value: '',
        address: '',
        airdropTokenId: '',
    });
    const [queryStringText, setQueryStringText] = useState(null);
    const [sendBchAddressError, setSendBchAddressError] = useState(false);
    const [sendBchAmountError, setSendBchAmountError] = useState(false);
    const [selectedCurrency, setSelectedCurrency] = useState(currency.ticker);

    // Support cashtab button from web pages
    const [txInfoFromUrl, setTxInfoFromUrl] = useState(false);

    // Show a confirmation modal on transactions created by populating form from web page button
    const [isModalVisible, setIsModalVisible] = useState(false);

    const [airdropFlag, setAirdropFlag] = useState(false);

    const userLocale = navigator.language;
    const clearInputForms = () => {
        setFormData({
            value: '',
            address: '',
        });
        setOpReturnMsg(''); // OP_RETURN message has its own state field
    };

    const checkForConfirmationBeforeSendXec = () => {
        if (txInfoFromUrl) {
            setIsModalVisible(true);
        } else if (cashtabSettings.sendModal) {
            setIsModalVisible(cashtabSettings.sendModal);
        } else {
            // if the user does not have the send confirmation enabled in settings then send directly
            send();
        }
    };

    const handleOk = () => {
        setIsModalVisible(false);
        send();
    };

    const handleCancel = () => {
        setIsModalVisible(false);
    };

    // If the balance has changed, unlock the UI
    // This is redundant, if backend has refreshed in 1.75s timeout below, UI will already be unlocked
    useEffect(() => {
        passLoadingStatus(false);
    }, [balances.totalBalance]);

    useEffect(() => {
        // Manually parse for txInfo object on page load when Send.js is loaded with a query string

        // if this was routed from Wallet screen's Reply to message link then prepopulate the address and value field
        if (location && location.state && location.state.replyAddress) {
            setFormData({
                address: location.state.replyAddress,
                value: `${fromSatoshisToXec(currency.dustSats).toString()}`,
            });
        }

        // if this was routed from the Contact List
        if (location && location.state && location.state.contactSend) {
            setFormData({
                address: location.state.contactSend,
            });
        }

        // if this was routed from the Airdrop screen's Airdrop Calculator then
        // switch to multiple recipient mode and prepopulate the recipients field
        if (
            location &&
            location.state &&
            location.state.airdropRecipients &&
            location.state.airdropTokenId
        ) {
            setIsOneToManyXECSend(true);
            setFormData({
                address: location.state.airdropRecipients,
                airdropTokenId: location.state.airdropTokenId,
            });

            // validate the airdrop outputs from the calculator
            handleMultiAddressChange({
                target: {
                    value: location.state.airdropRecipients,
                },
            });

            setAirdropFlag(true);
        }

        // Do not set txInfo in state if query strings are not present
        if (
            !window.location ||
            !window.location.hash ||
            window.location.hash === '#/send'
        ) {
            return;
        }

        const txInfoArr = window.location.hash.split('?')[1].split('&');

        // Iterate over this to create object
        const txInfo = {};
        for (let i = 0; i < txInfoArr.length; i += 1) {
            let txInfoKeyValue = txInfoArr[i].split('=');
            let key = txInfoKeyValue[0];
            let value = txInfoKeyValue[1];
            txInfo[key] = value;
        }
        console.log(`txInfo from page params`, txInfo);
        setTxInfoFromUrl(txInfo);
        populateFormsFromUrl(txInfo);
    }, []);

    function populateFormsFromUrl(txInfo) {
        if (txInfo && txInfo.address && txInfo.value) {
            setFormData({
                address: txInfo.address,
                value: txInfo.value,
            });
        }
    }

    function handleSendXecError(errorObj, oneToManyFlag) {
        // Set loading to false here as well, as balance may not change depending on where error occured in try loop
        passLoadingStatus(false);
        let message;
        if (
            errorObj.error &&
            errorObj.error.includes(
                'too-long-mempool-chain, too many unconfirmed ancestors [limit: 50] (code 64)',
            )
        ) {
            message = `The ${currency.ticker} you are trying to send has too many unconfirmed ancestors to send (limit 50). Sending will be possible after a block confirmation. Try again in about 10 minutes.`;
        } else {
            message =
                errorObj.message || errorObj.error || JSON.stringify(errorObj);
        }

        if (oneToManyFlag) {
            errorNotification(errorObj, message, 'Sending XEC one to many');
        } else {
            errorNotification(errorObj, message, 'Sending XEC');
        }
    }

    async function send() {
        setFormData({
            ...formData,
        });

        if (isOneToManyXECSend) {
            // this is a one to many XEC send transactions

            // ensure multi-recipient input is not blank
            if (!formData.address) {
                return;
            }

            // Event("Category", "Action", "Label")
            // Track number of XEC send-to-many transactions
            Event('Send.js', 'SendToMany', selectedCurrency);

            passLoadingStatus(true);
            const { address } = formData;

            //convert each line from TextArea input
            let addressAndValueArray = address.split('\n');

            try {
                const link = await sendXec(
                    chronik,
                    wallet,
                    nonSlpUtxos,
                    currency.defaultFee,
                    opReturnMsg,
                    true, // indicate send mode is one to many
                    addressAndValueArray,
                    null,
                    null,
                    false, // one to many tx msg can't be encrypted
                    airdropFlag,
                    formData.airdropTokenId,
                );
                sendXecNotification(link);
                clearInputForms();
                setAirdropFlag(false);
            } catch (e) {
                handleSendXecError(e, isOneToManyXECSend);
            }
        } else {
            // standard one to one XEC send transaction

            if (
                !formData.address ||
                !formData.value ||
                Number(formData.value) <= 0
            ) {
                return;
            }

            // Event("Category", "Action", "Label")
            // Track number of BCHA send transactions and whether users
            // are sending BCHA or USD
            Event('Send.js', 'Send', selectedCurrency);

            passLoadingStatus(true);
            const { address, value } = formData;

            // Get the param-free address
            let cleanAddress = address.split('?')[0];

            // Calculate the amount in BCH
            let bchValue = value;

            if (selectedCurrency !== 'XEC') {
                bchValue = fiatToCrypto(value, fiatPrice);
            }

            // encrypted message limit truncation
            let optionalOpReturnMsg;
            if (isEncryptedOptionalOpReturnMsg) {
                optionalOpReturnMsg = opReturnMsg.substring(
                    0,
                    currency.opReturn.encryptedMsgCharLimit,
                );
            } else {
                optionalOpReturnMsg = opReturnMsg;
            }

            try {
                const link = await sendXec(
                    chronik,
                    wallet,
                    nonSlpUtxos,
                    currency.defaultFee,
                    optionalOpReturnMsg,
                    false, // sendToMany boolean flag
                    null, // address array not applicable for one to many tx
                    cleanAddress,
                    bchValue,
                    isEncryptedOptionalOpReturnMsg,
                );
                sendXecNotification(link);
                clearInputForms();
            } catch (e) {
                handleSendXecError(e, isOneToManyXECSend);
            }
        }
    }

    const handleAddressChange = e => {
        const { value, name } = e.target;
        let error = false;
        let addressString = value;
        // parse address for parameters
        const addressInfo = parseAddressForParams(addressString);
        // validate address
        const isValid = isValidXecAddress(addressInfo.address);
        const { address, queryString, amount } = addressInfo;

        // If query string,
        // Show an alert that only amount and currency.ticker are supported
        setQueryStringText(queryString);

        // Is this valid address?
        if (!isValid) {
            error = `Invalid address`;
            // If valid address but token format
            if (isValidEtokenAddress(address)) {
                error = `eToken addresses are not supported for ${currency.ticker} sends`;
            }
        }
        setSendBchAddressError(error);

        // Set amount if it's in the query string
        if (amount !== null) {
            // Set currency to BCHA
            setSelectedCurrency(currency.ticker);

            // Use this object to mimic user input and get validation for the value
            let amountObj = {
                target: {
                    name: 'value',
                    value: amount,
                },
            };
            handleBchAmountChange(amountObj);
            setFormData({
                ...formData,
                value: amount,
            });
        }

        // Set address field to user input
        setFormData(p => ({
            ...p,
            [name]: value,
        }));
    };

    const handleMultiAddressChange = e => {
        const { value, name } = e.target;
        let error;

        if (!value) {
            error = 'Input must not be blank';
            setSendBchAddressError(error);
            return setFormData(p => ({
                ...p,
                [name]: value,
            }));
        }

        //convert each line from the <TextArea> input into array
        let addressStringArray = value.split('\n');
        const arrayLength = addressStringArray.length;

        // loop through each row in the <TextArea> input
        for (let i = 0; i < arrayLength; i++) {
            if (addressStringArray[i].trim() === '') {
                // if this line is a line break or bunch of spaces
                error = 'Empty spaces and rows must be removed';
                setSendBchAddressError(error);
                return setFormData(p => ({
                    ...p,
                    [name]: value,
                }));
            }

            let addressString = addressStringArray[i].split(',')[0];
            let valueString = addressStringArray[i].split(',')[1];

            const validAddress = isValidXecAddress(addressString);
            const validValueString = isValidXecSendAmount(valueString);

            if (!validAddress) {
                error = 'Ensure each XEC address is valid';
                setSendBchAddressError(error);
                return setFormData(p => ({
                    ...p,
                    [name]: value,
                }));
            }
            if (!validValueString) {
                error = 'Ensure each tx is at least 5.5 XEC';
                setSendBchAddressError(error);
                return setFormData(p => ({
                    ...p,
                    [name]: value,
                }));
            }
        }

        // If iterate to end of array with no errors, then there is no error msg
        setSendBchAddressError(false);

        // Set address field to user input
        setFormData(p => ({
            ...p,
            [name]: value,
        }));
    };

    const handleSelectedCurrencyChange = e => {
        setSelectedCurrency(e);
        // Clear input field to prevent accidentally sending 1 BCH instead of 1 USD
        setFormData(p => ({
            ...p,
            value: '',
        }));
    };

    const handleBchAmountChange = e => {
        const { value, name } = e.target;
        let bchValue = value;
        const error = shouldRejectAmountInput(
            bchValue,
            selectedCurrency,
            fiatPrice,
            balances.totalBalance,
        );
        setSendBchAmountError(error);

        setFormData(p => ({
            ...p,
            [name]: value,
        }));
    };

    const onMax = async () => {
        // Clear amt error
        setSendBchAmountError(false);
        // Set currency to BCH
        setSelectedCurrency(currency.ticker);
        try {
            const txFeeSats = calcFee(nonSlpUtxos);

            const txFeeBch = txFeeSats / 10 ** currency.cashDecimals;
            let value =
                balances.totalBalance - txFeeBch >= 0
                    ? (balances.totalBalance - txFeeBch).toFixed(
                          currency.cashDecimals,
                      )
                    : 0;

            setFormData({
                ...formData,
                value,
            });
        } catch (err) {
            console.log(`Error in onMax:`);
            console.log(err);
            message.error(
                'Unable to calculate the max value due to network errors',
            );
        }
    };
    // Display price in USD below input field for send amount, if it can be calculated
    let fiatPriceString = '';
    if (fiatPrice !== null && !isNaN(formData.value)) {
        if (selectedCurrency === currency.ticker) {
            // calculate conversion to fiatPrice
            fiatPriceString = `${(fiatPrice * Number(formData.value)).toFixed(
                2,
            )}`;

            // formats to fiat locale style
            fiatPriceString = formatFiatBalance(
                Number(fiatPriceString),
                userLocale,
            );

            // insert symbol and currency before/after the locale formatted fiat balance
            fiatPriceString = `${
                cashtabSettings
                    ? `${
                          currency.fiatCurrencies[cashtabSettings.fiatCurrency]
                              .symbol
                      } `
                    : '$ '
            } ${fiatPriceString} ${
                cashtabSettings && cashtabSettings.fiatCurrency
                    ? cashtabSettings.fiatCurrency.toUpperCase()
                    : 'USD'
            }`;
        } else {
            fiatPriceString = `${
                formData.value
                    ? formatFiatBalance(
                          Number(fiatToCrypto(formData.value, fiatPrice)),
                          userLocale,
                      )
                    : formatFiatBalance(0, userLocale)
            } ${currency.ticker}`;
        }
    }

    const priceApiError = fiatPrice === null && selectedCurrency !== 'XEC';

    return (
        <>
            <Modal
                title="Confirm Send"
                open={isModalVisible}
                onOk={handleOk}
                onCancel={handleCancel}
            >
                <p>
                    {isOneToManyXECSend
                        ? `are you sure you want to send the following One to Many transaction?
                    ${formData.address}`
                        : `Are you sure you want to send ${formData.value}${' '}
                  ${selectedCurrency} to ${formData.address}?`}
                </p>
            </Modal>
            <WalletInfoCtn>
                <WalletLabel
                    name={wallet.name}
                    cashtabSettings={cashtabSettings}
                    changeCashtabSettings={changeCashtabSettings}
                ></WalletLabel>
                {!balances.totalBalance ? (
                    <ZeroBalanceHeader>
                        You currently have 0 {currency.ticker}
                        <br />
                        Deposit some funds to use this feature
                    </ZeroBalanceHeader>
                ) : (
                    <>
                        <BalanceHeader
                            balance={balances.totalBalance}
                            ticker={currency.ticker}
                            cashtabSettings={cashtabSettings}
                        />

                        <BalanceHeaderFiat
                            balance={balances.totalBalance}
                            settings={cashtabSettings}
                            fiatPrice={fiatPrice}
                        />
                    </>
                )}
            </WalletInfoCtn>
            <SidePaddingCtn>
                <Row type="flex">
                    <Col span={24}>
                        <Form
                            style={{
                                width: 'auto',
                                marginTop: '40px',
                            }}
                        >
                            <SendAddressHeader>
                                {' '}
                                <FormLabel>Send to</FormLabel>
                                <TextAreaLabel>
                                    Multiple Recipients:&nbsp;&nbsp;
                                    <Switch
                                        defaultunchecked="true"
                                        checked={isOneToManyXECSend}
                                        onChange={() => {
                                            setIsOneToManyXECSend(
                                                !isOneToManyXECSend,
                                            );
                                            setIsEncryptedOptionalOpReturnMsg(
                                                false,
                                            );
                                        }}
                                        style={{
                                            marginBottom: '7px',
                                        }}
                                    />
                                </TextAreaLabel>
                            </SendAddressHeader>
                            <ExpandingAddressInputCtn open={isOneToManyXECSend}>
                                <SendInputCtn>
                                    <DestinationAddressSingleCtn>
                                        <DestinationAddressSingle
                                            style={{ marginBottom: '0px' }}
                                            loadWithCameraOpen={
                                                location &&
                                                location.state &&
                                                location.state.replyAddress
                                                    ? false
                                                    : scannerSupported
                                            }
                                            validateStatus={
                                                sendBchAddressError
                                                    ? 'error'
                                                    : ''
                                            }
                                            help={
                                                sendBchAddressError
                                                    ? sendBchAddressError
                                                    : ''
                                            }
                                            onScan={result =>
                                                handleAddressChange({
                                                    target: {
                                                        name: 'address',
                                                        value: result,
                                                    },
                                                })
                                            }
                                            inputProps={{
                                                placeholder: `Address`,
                                                name: 'address',
                                                onChange: e =>
                                                    handleAddressChange(e),
                                                required: true,
                                                value: formData.address,
                                            }}
                                        ></DestinationAddressSingle>
                                        <FormLabel>Amount</FormLabel>
                                        <SendBchInput
                                            activeFiatCode={
                                                cashtabSettings &&
                                                cashtabSettings.fiatCurrency
                                                    ? cashtabSettings.fiatCurrency.toUpperCase()
                                                    : 'USD'
                                            }
                                            validateStatus={
                                                sendBchAmountError
                                                    ? 'error'
                                                    : ''
                                            }
                                            help={
                                                sendBchAmountError
                                                    ? sendBchAmountError
                                                    : ''
                                            }
                                            onMax={onMax}
                                            inputProps={{
                                                name: 'value',
                                                dollar:
                                                    selectedCurrency === 'USD'
                                                        ? 1
                                                        : 0,
                                                placeholder: 'Amount',
                                                onChange: e =>
                                                    handleBchAmountChange(e),
                                                required: true,
                                                value: formData.value,
                                                disabled: priceApiError,
                                            }}
                                            selectProps={{
                                                value: selectedCurrency,
                                                disabled:
                                                    queryStringText !== null,
                                                onChange: e =>
                                                    handleSelectedCurrencyChange(
                                                        e,
                                                    ),
                                            }}
                                        ></SendBchInput>
                                    </DestinationAddressSingleCtn>

                                    {priceApiError && (
                                        <AlertMsg>
                                            Error fetching fiat price. Setting
                                            send by{' '}
                                            {currency.fiatCurrencies[
                                                cashtabSettings.fiatCurrency
                                            ].slug.toUpperCase()}{' '}
                                            disabled
                                        </AlertMsg>
                                    )}
                                </SendInputCtn>

                                <>
                                    <DestinationAddressMultiCtn>
                                        <DestinationAddressMulti
                                            validateStatus={
                                                sendBchAddressError
                                                    ? 'error'
                                                    : ''
                                            }
                                            help={
                                                sendBchAddressError
                                                    ? sendBchAddressError
                                                    : ''
                                            }
                                            inputProps={{
                                                placeholder: `One address & value per line, separated by comma \ne.g. \necash:qpatql05s9jfavnu0tv6lkjjk25n6tmj9gkpyrlwu8,500 \necash:qzvydd4n3lm3xv62cx078nu9rg0e3srmqq0knykfed,700`,
                                                name: 'address',
                                                onChange: e =>
                                                    handleMultiAddressChange(e),
                                                required: true,
                                                value: formData.address,
                                            }}
                                        ></DestinationAddressMulti>
                                    </DestinationAddressMultiCtn>
                                </>
                                <AmountPreviewCtn>
                                    {!priceApiError && !isOneToManyXECSend && (
                                        <>
                                            <LocaleFormattedValue>
                                                {formatBalance(
                                                    formData.value,
                                                    userLocale,
                                                )}{' '}
                                                {selectedCurrency}
                                            </LocaleFormattedValue>
                                            <ConvertAmount>
                                                {fiatPriceString !== '' && '='}{' '}
                                                {fiatPriceString}
                                            </ConvertAmount>
                                        </>
                                    )}
                                </AmountPreviewCtn>
                            </ExpandingAddressInputCtn>

                            {queryStringText && (
                                <Alert
                                    message={`You are sending a transaction to an address including query parameters "${queryStringText}." Only the "amount" parameter, in units of ${currency.ticker} satoshis, is currently supported.`}
                                    type="warning"
                                />
                            )}
                            <div
                                style={{
                                    paddingTop: '1rem',
                                }}
                            >
                                {!balances.totalBalance ||
                                apiError ||
                                sendBchAmountError ||
                                sendBchAddressError ||
                                priceApiError ? (
                                    <DisabledButton>Send</DisabledButton>
                                ) : (
                                    <>
                                        {txInfoFromUrl ? (
                                            <PrimaryButton
                                                onClick={() =>
                                                    checkForConfirmationBeforeSendXec()
                                                }
                                            >
                                                Send
                                            </PrimaryButton>
                                        ) : (
                                            <PrimaryButton
                                                onClick={() => {
                                                    checkForConfirmationBeforeSendXec();
                                                }}
                                            >
                                                Send
                                            </PrimaryButton>
                                        )}
                                    </>
                                )}
                            </div>

                            <CustomCollapseCtn
                                panelHeader={
                                    <PanelHeaderCtn>
                                        <ThemedMailOutlined /> Message
                                    </PanelHeaderCtn>
                                }
                                optionalDefaultActiveKey={
                                    location &&
                                    location.state &&
                                    location.state.replyAddress
                                        ? ['1']
                                        : ['0']
                                }
                                optionalKey="1"
                            >
                                <AntdFormWrapper
                                    style={{
                                        marginBottom: '20px',
                                    }}
                                >
                                    <TextAreaLabel>
                                        Message:&nbsp;&nbsp;
                                        <Switch
                                            disabled={isOneToManyXECSend}
                                            style={{
                                                marginBottom: '7px',
                                            }}
                                            checkedChildren="Private"
                                            unCheckedChildren="Public"
                                            defaultunchecked="true"
                                            checked={
                                                isEncryptedOptionalOpReturnMsg
                                            }
                                            onChange={() => {
                                                setIsEncryptedOptionalOpReturnMsg(
                                                    prev => !prev,
                                                );
                                                setIsOneToManyXECSend(false);
                                            }}
                                        />
                                    </TextAreaLabel>
                                    {isEncryptedOptionalOpReturnMsg ? (
                                        <Alert
                                            style={{
                                                marginBottom: '10px',
                                            }}
                                            description="Please note encrypted messages can only be sent to wallets with at least 1 outgoing transaction."
                                            type="warning"
                                            showIcon
                                        />
                                    ) : (
                                        <Alert
                                            style={{
                                                marginBottom: '10px',
                                            }}
                                            description="Please note this message will be public."
                                            type="warning"
                                            showIcon
                                        />
                                    )}
                                    <TextArea
                                        name="opReturnMsg"
                                        placeholder={
                                            isEncryptedOptionalOpReturnMsg
                                                ? `(max ${currency.opReturn.encryptedMsgCharLimit} characters)`
                                                : location &&
                                                  location.state &&
                                                  location.state.airdropTokenId
                                                ? `(max ${currency.opReturn.unencryptedAirdropMsgCharLimit} characters)`
                                                : `(max ${currency.opReturn.unencryptedMsgCharLimit} characters)`
                                        }
                                        value={
                                            opReturnMsg
                                                ? isEncryptedOptionalOpReturnMsg
                                                    ? opReturnMsg.substring(
                                                          0,
                                                          currency.opReturn
                                                              .encryptedMsgCharLimit +
                                                              1,
                                                      )
                                                    : opReturnMsg
                                                : ''
                                        }
                                        onChange={e =>
                                            setOpReturnMsg(e.target.value)
                                        }
                                        showCount
                                        maxLength={
                                            isEncryptedOptionalOpReturnMsg
                                                ? currency.opReturn
                                                      .encryptedMsgCharLimit
                                                : location &&
                                                  location.state &&
                                                  location.state.airdropTokenId
                                                ? currency.opReturn
                                                      .unencryptedAirdropMsgCharLimit
                                                : currency.opReturn
                                                      .unencryptedMsgCharLimit
                                        }
                                        onKeyDown={e =>
                                            e.keyCode == 13
                                                ? e.preventDefault()
                                                : ''
                                        }
                                    />
                                </AntdFormWrapper>
                            </CustomCollapseCtn>
                            {apiError && <ApiError />}
                        </Form>
                    </Col>
                </Row>
            </SidePaddingCtn>
        </>
    );
};

/*
passLoadingStatus must receive a default prop that is a function
in order to pass the rendering unit test in Send.test.js

status => {console.log(status)} is an arbitrary stub function
*/

SendBCH.defaultProps = {
    passLoadingStatus: status => {
        console.log(status);
    },
};

SendBCH.propTypes = {
    passLoadingStatus: PropTypes.func,
};

export default SendBCH;
